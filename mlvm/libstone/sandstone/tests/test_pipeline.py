"""End-to-end pipeline test: .dsymspec -> .symas -> assembled bytes.

Drives scripts/spec-compiler.py (Lua DSL step) plus the freshly built
sandstone-spec-compiler binary, then assembles a small source with the
freshly built sandstone-as CLI and checks the emitted bytes.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile


X86_SOURCE = """\
.text
.globl _start
_start:
    mov eax, ebx
    nop
    call target
    ret
target:
    nop
"""

RISCV_SOURCE = """\
.text
.globl _start
_start:
    add x1, x2, x3
    nop
    jalr x1, x2, 0
target:
    nop
"""

RISCV_JAL_SOURCE = """\
.text
_start:
    jal x1, target
target:
    nop
"""


def run(cmd, **kwargs):
    proc = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
    if proc.returncode != 0:
        raise AssertionError(
            "command failed: %s\nstdout: %s\nstderr: %s"
            % (" ".join(cmd), proc.stdout, proc.stderr)
        )
    return proc


def find_binary(build_dir, name):
    for root, _dirs, files in os.walk(build_dir):
        if name in files:
            return os.path.join(root, name)
    raise AssertionError("binary %s not found under %s" % (name, build_dir))


def build_bank(source_dir, compiler_bin, spec, out_path):
    script = os.path.join(source_dir, "scripts", "spec-compiler.py")
    run([sys.executable, script, "--compiler", compiler_bin,
         "--validate", "-o", out_path, spec])


def assemble(asm_bin, bank, source, out_path, syntax=None, fmt="bin"):
    with tempfile.NamedTemporaryFile("w", suffix=".s",
                                     delete=False) as f:
        f.write(source)
        src_path = f.name
    try:
        cmd = [asm_bin, "--bank", bank, "-f", fmt, "-o", out_path, src_path]
        if syntax:
            cmd += ["--syntax", syntax]
        run(cmd)
    finally:
        os.unlink(src_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--source-dir", required=True)
    args = parser.parse_args()

    compiler_bin = find_binary(args.build_dir, "sandstone-spec-compiler")
    asm_bin = find_binary(args.build_dir, "sandstone-as")
    tmp = tempfile.mkdtemp(prefix="sandstone-pipeline-")

    # x86 (Intel syntax): mov r32,r32 = 0x89 /r, nop, call rel32, ret.
    x86_spec = os.path.join(args.source_dir, "compiler", "stdspec",
                            "x86.dsymspec")
    x86_bank = os.path.join(tmp, "x86.symas")
    build_bank(args.source_dir, compiler_bin, x86_spec, x86_bank)
    x86_out = os.path.join(tmp, "x86.bin")
    assemble(asm_bin, x86_bank, X86_SOURCE, x86_out, syntax="intel")
    with open(x86_out, "rb") as f:
        code = f.read()
    # mov eax,ebx -> 89 d8 ; nop -> 90 ; call +6 -> e8 06 00 00 00 ;
    # ret -> c3 ; nop -> 90
    assert code[:3] == bytes([0x89, 0xD8, 0x90]), code.hex()
    assert code[3] == 0xE8, code.hex()
    disp = int.from_bytes(code[4:8], "little", signed=True)
    assert disp == 1, (code.hex(), disp)  # target is 1 byte past next insn
    assert code[8:] == bytes([0xC3, 0x90]), code.hex()

    # Same bank through the obj (JSON) output: one kept relocation is
    # impossible here (all labels local), so relocs must be empty and the
    # symbol table must carry _start/target.
    x86_obj = os.path.join(tmp, "x86.obj.json")
    assemble(asm_bin, x86_bank, X86_SOURCE, x86_obj, syntax="intel",
             fmt="obj")
    with open(x86_obj, encoding="utf-8") as f:
        obj = json.load(f)
    assert obj["format"] == "sandstone-obj-1"
    names = {s["name"] for s in obj["symbols"]}
    assert {"_start", "target"} <= names, names
    assert obj["relocs"] == [], obj["relocs"]

    # External symbol keeps a relocation in obj output but fails in bin.
    ext_src = ".text\ncall puts\n"
    ext_obj = os.path.join(tmp, "ext.obj.json")
    assemble(asm_bin, x86_bank, ext_src, ext_obj, syntax="intel", fmt="obj")
    with open(ext_obj, encoding="utf-8") as f:
        ext = json.load(f)
    assert len(ext["relocs"]) == 1, ext["relocs"]
    assert ext["relocs"][0]["symbol"] == "puts"
    # Note: the CLI takes files; feed the source via a temp file.
    with tempfile.NamedTemporaryFile("w", suffix=".s",
                                     delete=False) as f:
        f.write(ext_src)
        ext_path = f.name
    try:
        proc = subprocess.run([asm_bin, "--bank", x86_bank, "-f", "bin",
                               "-o", os.path.join(tmp, "ext.bin"),
                               ext_path],
                              capture_output=True, text=True)
    finally:
        os.unlink(ext_path)
    assert proc.returncode != 0, "flat output must reject externals"

    # riscv64: add is 4 bytes; jalr with a constant encodes inline.
    # add x1,x2,x3 = 0x003100b3 -> b3 00 31 00 (little endian).
    rv_spec = os.path.join(args.source_dir, "compiler", "stdspec",
                           "riscv64.dsymspec")
    rv_bank = os.path.join(tmp, "riscv64.symas")
    build_bank(args.source_dir, compiler_bin, rv_spec, rv_bank)
    rv_out = os.path.join(tmp, "rv.bin")
    assemble(asm_bin, rv_bank, RISCV_SOURCE, rv_out)
    with open(rv_out, "rb") as f:
        rv = f.read()
    assert rv[:4] == bytes.fromhex("b3003100"), rv.hex()
    assert len(rv) == 16, rv.hex()

    # jal against a local label needs linker-style immediate splicing, so
    # flat output keeps a relocation (obj) instead of patching it.
    rv_jal_obj = os.path.join(tmp, "rv_jal.obj.json")
    assemble(asm_bin, rv_bank, RISCV_JAL_SOURCE, rv_jal_obj, fmt="obj")
    with open(rv_jal_obj, encoding="utf-8") as f:
        jal_obj = json.load(f)
    assert len(jal_obj["relocs"]) == 1, jal_obj["relocs"]
    assert jal_obj["relocs"][0]["type"] == "R_RISCV_CALL"
    assert jal_obj["relocs"][0]["symbol"] == "target"

    print("PIPELINE_OK")


if __name__ == "__main__":
    main()
