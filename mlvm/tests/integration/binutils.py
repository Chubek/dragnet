"""Library-backed tool contracts, independent native-tool interoperability."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile

bin_dir = Path(sys.argv[1]).resolve()


def run(tool, *args, status=0, cwd=None):
    result = subprocess.run([str(bin_dir / tool), *map(str, args)], capture_output=True, timeout=15, cwd=cwd)
    assert result.returncode == status, (tool, args, status, result.returncode, result.stderr)
    return result


def contract(bits=64, big=False):
    return f'''version = 1
class = {bits}
machine = {62 if bits == 64 else 3}
endianness = "{'big' if big else 'little'}"
flags = 0
osabi = 0
abi_version = 0
text_alignment = 4
[[relocations]]
type = 2
name = "pc32"
kind = "pc_relative"
storage_bytes = 4
bit_offset = 0
bits = 32
scale = 1
signed = true
implicit_addend_signed = true
'''


with tempfile.TemporaryDirectory(prefix="mlvm-binutils-") as directory:
    root = Path(directory)
    source = root / "source.s"
    source.write_text(".globl caller\ncaller:\ncall answer\nlocal:\nret\n.data\n.long 7\n")
    caller = root / "caller.o"
    run("mlvm-as", source, "-o", caller)
    source.write_text(".globl answer\n.type answer,@function\nanswer:\nmov $42,%eax\nret\n")
    answer = root / "an answer with a long name.o"
    run("mlvm-as", source, "-o", answer)
    assert b"UND answer" in run("mlvm-nm", "-gu", caller).stdout
    assert b"local" not in run("mlvm-nm", "--extern-only", caller).stdout
    assert b"answer" not in run("mlvm-nm", "--defined-only", caller).stdout
    run("mlvm-nm", "-u", "--defined-only", caller, status=2)
    run("mlvm-nm", "--extern", caller, status=2)
    run("mlvm-nm", "--extern-only=yes", caller, status=2)
    run("mlvm-nm", "-n", "-p", caller, status=2)
    # View flags actually select output instead of being accepted and ignored.
    sections = run("mlvm-readelf", "-S", caller).stdout
    assert b"section " in sections and b"UND answer" not in sections and b"reloc " not in sections
    relocs = run("mlvm-readobj", "--relocs", caller).stdout
    assert b"reloc " in relocs and b"section " not in relocs
    assert b"Contents of" not in run("mlvm-objdump", "-t", caller).stdout
    contents = run("mlvm-objdump", "-sj.text", caller).stdout
    assert b"Contents of .text" in contents and b"Contents of .data" not in contents
    assert b"call" in run("mlvm-objdump", "-d", caller).stdout
    stripped = root / "stripped.o"
    run("mlvm-strip", caller, "-o", stripped)
    assert b"local" not in run("mlvm-nm", stripped).stdout and b"answer" in run("mlvm-nm", stripped).stdout
    run("mlvm-strip", "--strip-all", caller, "-o", stripped, status=1)
    run("mlvm-strip", "--strip-all", answer, "-o", stripped)
    assert run("mlvm-nm", stripped).stdout == b""
    archive, repeated = root / "library.a", root / "again.a"
    run("mlvm-ar", "q", caller, answer, "-o", archive)
    run("mlvm-ar", "q", caller, answer, "-o", repeated)
    assert archive.read_bytes() == repeated.read_bytes()
    assert run("mlvm-ar", "t", archive).stdout == b"caller.o\nan answer with a long name.o\n"
    assert b"answer" in run("mlvm-nm", archive).stdout
    assert b"library.a(caller.o)" in run("mlvm-size", archive).stdout
    # The baseline static/relocatable linker also reads indexed long-name archives.
    combined = root / "combined.o"
    run("mlvm-ld", "-r", caller, archive, "-o", combined)
    assert b"UND answer" not in run("mlvm-nm", combined).stdout
    extracted = root / "extracted"
    extracted.mkdir()
    run("mlvm-ar", "x", archive, answer.name, "--output-dir", extracted)
    assert (extracted / answer.name).read_bytes() == answer.read_bytes()
    assert b"answer" in run("mlvm-nm", extracted / answer.name).stdout
    assert run("mlvm-ar", "p", archive, answer.name).stdout == (extracted / answer.name).read_bytes()
    run("mlvm-ar", "x", archive, "missing.o", "--output-dir", extracted, status=1)
    run("mlvm-ar", "q", caller, caller, "-o", repeated, status=2)
    removed = root / "removed.a"
    run("mlvm-ar", "d", archive, "caller.o", "-o", removed)
    assert run("mlvm-ar", "t", removed).stdout == (answer.name + "\n").encode()
    run("mlvm-ar", "r", removed, caller, "-o", repeated)
    assert b"caller.o" in run("mlvm-ar", "t", repeated).stdout
    run("mlvm-ar", "r", archive, caller, "-o", archive, status=2)
    data_member = root / "notes with a long name.txt"
    data_member.write_bytes(b"arbitrary\0payload\xff")
    run("mlvm-ar", "q", data_member, answer, "-o", repeated)
    assert run("mlvm-ar", "p", repeated, data_member.name).stdout == data_member.read_bytes()
    run("mlvm-ar", "x", repeated, data_member.name, "--output-dir", extracted)
    assert (extracted / data_member.name).read_bytes() == data_member.read_bytes()
    # Framing-only extraction accepts raw members, but rejects path traversal
    # and protects an archive that happens to share an extracted basename.
    def ar_record(name, payload):
        fields = (name.ljust(16), "0".ljust(12), "0".ljust(6), "0".ljust(6), "100644".ljust(8), str(len(payload)).ljust(10), "`\n")
        return "".join(fields).encode() + payload + (b"\n" if len(payload) % 2 else b"")

    unsafe = root / "unsafe.a"
    unsafe.write_bytes(b"!<arch>\n" + ar_record("safe.txt/", b"safe") + ar_record("../escape/", b"bad"))
    run("mlvm-ar", "x", unsafe, "--output-dir", extracted, status=1)
    assert not (extracted / "safe.txt").exists() and not (root / "escape").exists()
    unsafe.write_bytes(b"!<arch>\n" + ar_record("unsafe.a/", b"bad"))
    run("mlvm-ar", "x", unsafe, "--output-dir", root, status=2)
    if shutil.which("ar") and shutil.which("nm"):
        result = subprocess.run(["nm", "-s", str(archive)], capture_output=True, check=True)
        assert b"answer in " + answer.name.encode() in result.stdout
        assert answer.name.encode() in subprocess.run(["ar", "t", str(archive)], capture_output=True, check=True).stdout
        native_archive = root / "native.a"
        subprocess.run(["ar", "rcs", str(native_archive), str(answer)], check=True)
        assert run("mlvm-ar", "t", native_archive).stdout == (answer.name + "\n").encode()
    config = root / "x86.toml"
    config.write_text(contract())
    image, map_file = root / "image.bin", root / "map.txt"
    run("mlvm-ld", "--target-config", config, "-O", "binary", "--base-address=0x1000", caller, removed, "--map", map_file, "-o", image)
    # caller's text (6 bytes), data (4), then answer's 16-byte-aligned text.
    assert image.read_bytes()[0] == 0xE8
    assert struct.unpack_from("<i", image.read_bytes(), 1)[0] == 11
    assert b"symbol answer 4112" in map_file.read_bytes()
    run("mlvm-ld", "--target-config", config, "-O", "binary", caller, "-o", image, status=1)
    run("mlvm-ld", "--target-config", config, "-O", "binary", "--defsym=answer=0x100", caller, "-o", image)
    run("mlvm-ld", "--target-config", config, caller, "-o", image, status=2)
    run("mlvm-ld", "--target-config", config, "-O", "binary", caller, "-o", config, status=2)
    run("mlvm-ld", "--target-config", config, "-O", "binary", caller, "--map", image, "-o", image, status=2)
    config.write_text(contract().replace("bits = 32", "bits = 0"))
    run("mlvm-ld", "--target-config", config, "-O", "binary", caller, "-o", image, status=1)
    config.write_text(contract().replace("type = 2", "type = 3"))
    assert b"missing ELF relocation encoding" in run("mlvm-ld", "--target-config", config, "-O", "binary", "--defsym=answer=256", caller, "-o", image, status=1).stderr
    # Validate the shipped x86-64 contract using independent field arithmetic.
    example = Path(__file__).resolve().parents[2] / "examples" / "targets" / "x86_64.toml"
    source.write_text(".globl pointer\npointer:\ncall answer\nret\n.data\n.quad answer\n")
    pointer = root / "pointer.o"
    run("mlvm-as", source, "-o", pointer)
    run("mlvm-ld", "--target-config", example, "-O", "binary", "--base-address=0x1000", pointer, removed, "-o", image)
    assert struct.unpack_from("<i", image.read_bytes(), 1)[0] == 11
    assert struct.unpack_from("<Q", image.read_bytes(), 6)[0] == 0x1010
    payload = root / "strings.bin"
    payload.write_bytes(b"\0hello\0tail")
    assert run("mlvm-strings", "-tx", payload).stdout == b"1 hello\n7 tail\n"
    payload.write_bytes(b"\0\0" + "wide".encode("utf-16-le") + b"x")
    assert run("mlvm-strings", "-el", "-td", payload).stdout == b"2 wide\n"
    payload.write_bytes("wide".encode("utf-16-be"))
    assert run("mlvm-strings", "--encoding=b", payload).stdout == b"wide\n"
    run("mlvm-strings", "-e", "unicode", payload, status=2)
    response = root / "args.rsp"
    response.write_text(f'--encoding=b "{payload}"')
    assert run("mlvm-strings", "@" + str(response)).stdout == b"wide\n"
    response.write_text(f'"{answer}" -o "{response}"')
    run("mlvm-objcopy", "@" + str(response), status=2)
    if hasattr(os, "link"):
        alias = root / "alias.o"
        os.link(answer, alias)
        run("mlvm-objcopy", answer, "-o", alias, status=2)
    bad = root / "bad.a"
    bad.write_bytes(archive.read_bytes()[:-1])
    run("mlvm-ar", "t", bad, status=3)
    bad.write_bytes(b"!<thin>\n")
    run("mlvm-readobj", bad, status=3)
    positional = root / "--sections"
    positional.write_bytes(caller.read_bytes())
    assert b"caller" in run("mlvm-readobj", "--", positional.name, cwd=root).stdout
    # Native i386 REL/BSS ingestion and emission needs no 32-bit runtime.
    if shutil.which("as") and sys.platform.startswith("linux"):
        source.write_text(".globl foo\n.text\nfoo:\n.long external\n.bss\n.balign 8\n.space 24\n")
        narrow = root / "narrow.o"
        result = subprocess.run(["as", "--32", str(source), "-o", str(narrow)], capture_output=True)
        if result.returncode == 0:
            assert b"ELF32" in run("mlvm-readobj", narrow).stdout
            assert b"addend=implicit (REL)" in run("mlvm-readelf", "-r", narrow).stdout
            assert b".bss\t24" in run("mlvm-size", narrow).stdout
            assert b"text\tdata\tbss\ttotal" in run("mlvm-size", "-B", narrow).stdout
            run("mlvm-objdump", "-d", narrow, status=1)
            run("mlvm-objcopy", narrow, "--rename-section=.text=.code", "-o", combined)
            assert b".code" in run("mlvm-readelf", "-S", combined).stdout
            if shutil.which("readelf"):
                info = subprocess.run(["readelf", "-r", str(combined)], capture_output=True, check=True).stdout
                assert b".rel.code" in info and b"R_386_32" in info
            config.write_text(contract(32).replace('kind = "pc_relative"', 'kind = "absolute"').replace("type = 2", "type = 1").replace("signed = true", "signed = false"))
            run("mlvm-ld", "--target-config", config, "-O", "binary", "--defsym=external=0x1234", narrow, "-o", image)
            assert image.read_bytes()[:4] == b"\x34\x12\0\0"
            assert image.read_bytes()[8:32] == bytes(24)
            # Some assemblers add allocated GNU property notes after BSS.
            assert len(image.read_bytes()) >= 32
