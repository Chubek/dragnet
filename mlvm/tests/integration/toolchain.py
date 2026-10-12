"""CLI contracts and real compile/assemble/archive/link/execute regression tests."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

bin_dir = Path(sys.argv[1]).resolve()
cephyr_enabled = sys.argv[2] == "ON"


def run(tool, *args, status=0):
    result = subprocess.run([str(bin_dir / tool), *map(str, args)], capture_output=True, timeout=15)
    if result.returncode != status:
        raise AssertionError(f"{tool} {args}: expected {status}, got {result.returncode}\n{result.stderr.decode()}")
    return result


with tempfile.TemporaryDirectory(prefix="mlvm-test-") as directory:
    root = Path(directory)
    start = root / "start.s"
    start.write_text(".globl _start\n_start:\ncall answer\nmov %eax, %edi\nmov $60, %eax\nsyscall\n")
    answer = root / "answer.s"
    answer.write_text(".globl answer\n.type answer, @function\nanswer:\nmov $42, %eax\nret\n.size answer, .-answer\n")
    start_o, answer_o = root / "start.o", root / "answer.o"
    run("mlvm-as", start, "-o", start_o)
    run("mlvm-as", answer, "-o", answer_o)
    repeat = root / "repeat.o"
    run("mlvm-as", answer, "-o", repeat)
    assert answer_o.read_bytes() == repeat.read_bytes()
    for tool in ("mlvm-nm", "mlvm-readobj", "mlvm-readelf", "mlvm-objdump"):
        assert b"answer" in run(tool, answer_o).stdout
    assert b"mov $0x2a, %eax" in run("mlvm-objdump", "-d", answer_o).stdout
    assert b".text\t6" in run("mlvm-size", answer_o).stdout
    assert b"answer" in run("mlvm-strings", answer_o).stdout
    assert b"answer" not in run("mlvm-strings", "-n", "20", answer_o).stdout
    run("mlvm-strings", "-n", "0", answer_o, status=2)
    run("mlvm-as", "--unsupported", start, status=2)
    run("mlvm-as", "--target=arm64", start, status=2)
    run("mlvm-as", start, "-o", start, status=2)
    run("mlvm-as", start, "-o", root / "missing" / "out.o", status=3)
    original = start.read_bytes()
    if hasattr(os, "symlink"):
        alias = root / "alias.s"
        alias.symlink_to(start)
        run("mlvm-as", start, "-o", alias, status=2)
        assert start.read_bytes() == original
    run("mlvm-ld", start_o, "-o", root / "undefined", status=1)
    run("mlvm-ld", answer_o, answer_o, "-r", "-o", root / "duplicate.o", status=1)
    linked = root / "combined.o"
    run("mlvm-ld", "-r", start_o, answer_o, "-o", linked)
    assert b"UND answer" not in run("mlvm-nm", linked).stdout
    executable = root / "program"
    run("mlvm-ld", start_o, answer_o, "-o", executable)
    if platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64"):
        assert subprocess.run([str(executable)], timeout=10).returncode == 42
    # GNU tooling is an independent format/encoding oracle when available.
    if shutil.which("readelf"):
        info = subprocess.run(["readelf", "-h", "-s", "-r", str(start_o)], capture_output=True, check=True)
        assert b"R_X86_64_PC32" in info.stdout
    archive = root / "libanswer.a"
    run("mlvm-ar", "q", "-o", archive, answer_o)
    assert run("mlvm-ar", "t", archive).stdout == b"answer.o\n"
    run("mlvm-ld", start_o, archive, "-o", root / "archived")
    assert (root / "archived").read_bytes() == executable.read_bytes()
    raw = root / "text.bin"
    run("mlvm-objcopy", "--only-section=.text", "-O", "binary", answer_o, "-o", raw)
    assert raw.read_bytes() == b"\xb8\x2a\0\0\0\xc3"
    renamed = root / "renamed.o"
    run("mlvm-objcopy", "--rename-section=.text=.code", answer_o, "-o", renamed)
    assert b".code" in run("mlvm-readobj", renamed).stdout
    removed = root / "removed.o"
    run("mlvm-objcopy", "--remove-section=.text", answer_o, "-o", removed)
    assert b"answer" not in run("mlvm-nm", removed).stdout
    run("mlvm-strip", answer_o, "-o", root / "stripped.o")
    assert b"answer" in run("mlvm-nm", root / "stripped.o").stdout
    bad = root / "bad.o"
    bad.write_bytes(b"\x7fELF")
    run("mlvm-readelf", bad, status=3)
    run("mlvm-readelf", root / "does-not-exist", status=3)
    response = root / "args.rsp"
    response.write_text(f'"{answer}" -o "{repeat}"')
    run("mlvm-as", "@" + str(response))
    response.write_text(f'"{answer}" -o "{response}"')
    run("mlvm-as", "@" + str(response), status=2)
    response.write_text("@" + str(response))
    run("mlvm-as", "@" + str(response), status=2)
    ir = root / "input.mlvm"
    ir.write_text('mlvm 1 { block() { %a:"i32" = "arith.constant"() ["value"="3"]; } }')
    run("mlvm", "--verify", ir)
    normalized = root / "output.mlvm"
    run("mlvm", ir, "-o", normalized)
    assert run("mlvm", normalized).stdout == normalized.read_bytes()
    assert b"arith.constant" not in run("mlvm", "--passes=constant-fold,dce", ir).stdout
    assert b"// before constant-fold" in run("mlvm", "--passes=constant-fold", "--dump-passes", ir).stderr
    run("mlvm", "--passes=unknown", ir, status=2)
    wide = root / "wide.mlvm"
    wide.write_text(
        'mlvm 1 { block() { "builtin.func"() ["sym"="f", "result"="i32"] region { block() {'
        ' %a:"i32" = "arith.constant"() ["value"="20"];'
        ' %b:"i32" = "arith.constant"() ["value"="6"];'
        ' %c:"i32" = "arith.divsi"(%a, %b) [];'
        ' %d:"i32" = "arith.remsi"(%a, %b) [];'
        ' %e:"i32" = "arith.and"(%a, %b) [];'
        ' %f:"i32" = "arith.or"(%a, %b) [];'
        ' %g:"i32" = "arith.xor"(%a, %b) [];'
        ' %h:"i32" = "arith.addi"(%c, %d) [];'
        ' "func.return"(%h) []; } }; } }'
    )
    folded = run("mlvm", "--passes=constant-fold", wide).stdout
    for expected in (b'"value"="3"', b'"value"="2"', b'"value"="4"', b'"value"="22"', b'"value"="18"'):
        assert expected in folded, folded
    assert b"arith.divsi" not in folded and b"arith.and" not in folded
    undef = root / "divzero.mlvm"
    undef.write_text(
        'mlvm 1 { block() { "builtin.func"() ["sym"="f", "result"="i32"] region { block() {'
        ' %a:"i32" = "arith.constant"() ["value"="1"];'
        ' %b:"i32" = "arith.constant"() ["value"="0"];'
        ' %c:"i32" = "arith.divsi"(%a, %b) [];'
        ' "func.return"(%c) []; } }; } }'
    )
    # Division by zero is undefined: folding leaves the operation unfolded.
    assert b"arith.divsi" in run("mlvm", "--passes=constant-fold", undef).stdout
    canon = root / "canon.mlvm"
    canon.write_text(
        'mlvm 1 { block() { "builtin.func"() ["sym"="f", "result"="i32"] region { block() {'
        ' %z:"i32" = "arith.constant"() ["value"="0"];'
        ' %o:"i32" = "arith.constant"() ["value"="1"];'
        ' %x:"i32" = "arith.constant"() ["value"="41"];'
        ' %a:"i32" = "arith.addi"(%x, %z) [];'
        ' %b:"i32" = "arith.muli"(%a, %o) [];'
        ' "func.return"(%b) []; } }; } }'
    )
    simplified = run("mlvm", "--passes=canonicalize", canon).stdout
    assert b"arith.addi" not in simplified and b"arith.muli" not in simplified
    run("mlvm", "--passes=canonicalize", ir)
    extended = root / "extended.s"
    extended.write_text(
        ".globl _start\n_start:\nmov $60, %eax\nadd $1, %eax\nsub $2, %ebx\npush %ebp\npop %ebp\nmov %eax, %edi\nsyscall\n"
    )
    extended_o = root / "extended.o"
    run("mlvm-as", extended, "-o", extended_o)
    disassembly = run("mlvm-objdump", "-d", extended_o).stdout
    assert b"add $0x1, %eax" in disassembly and b"sub $0x2, %ebx" in disassembly
    assert b"push %ebp" in disassembly and b"pop %ebp" in disassembly
    if cephyr_enabled:
        source = root / "answer.c"
        header = root / "value.h"
        header.write_text("#ifndef VALUE_H\n#define VALUE_H\n#define VALUE (6 * 7)\n#endif\n")
        source.write_text('#include "value.h"\nint answer(void) { return VALUE; }\n')
        run("cephyr", "-fsyntax-only", "-std=c17", "-ffreestanding", source)
        assert b"(6 * 7)" in run("cephyr", "-E", "-I", root, source).stdout
        run("cephyr", "-E", source, "-o", header, status=2)
        run("cephyr", "-S", source, "-o", root / "compiled.s")
        run("cephyr", "-c", "-O1", source, "-o", root / "compiled.o")
        run("cephyr", "--emit-ir", source, "-o", root / "compiled.mlvm")
        run("mlvm", "--verify", root / "compiled.mlvm")
        run("mlvm-ld", start_o, root / "compiled.o", "-o", root / "compiled")
        if platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64"):
            assert subprocess.run([str(root / "compiled")], timeout=10).returncode == 42
        source.write_text("int answer(void) { return FLAG; }\n")
        run("cephyr", "-D", "FLAG=42", "-fsyntax-only", source)
        run("cephyr", "-DFLAG=42", "-UFLAG", "-fsyntax-only", source, status=1)
        run("cephyr", "-include", header, "-D", "FLAG=VALUE", "-fsyntax-only", source)
        source.write_text("int answer(void) { return (20 / 4) + (48 & 15) + (1 ^ 2) + (1 << 4) - (20 % 6); }\n")
        run("cephyr", "-fsyntax-only", source)
        run("cephyr", "-S", source, "-o", root / "wide.s")
        assert b"mov $22, %eax" in (root / "wide.s").read_bytes()
        source.write_text("int f(void) { return 1 << 40; }\n")
        assert b"shift" in run("cephyr", "-fsyntax-only", source, status=1).stderr
        source.write_text("int f(void) { return 2147483647 + 1; }\n")
        result = run("cephyr", "-fsyntax-only", source, status=1)
        assert b"overflow" in result.stderr
        source.write_text("struct S { int x; };\n")
        run("cephyr", "--parse-only", source)
        run("cephyr", "-fsyntax-only", source, status=1)
        header.write_text('#include "value.h"\n')
        source.write_text('#include "value.h"\n')
        assert b"cycle" in run("cephyr", "-E", source, status=1).stderr
        run("cephyr", "-std=c23", source, status=2)
