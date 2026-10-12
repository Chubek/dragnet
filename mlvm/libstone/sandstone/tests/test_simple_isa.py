"""Golden instruction bytes checked against llvm-mc for each target."""

import sys
from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "isa"))
from simple_isa import ISAError, available as _available, load_arch as _load_arch, parse

_ISA_DIR = REPO_ROOT / "isa"


def available(directory=None):
    return _available(_ISA_DIR if directory is None else directory)


def load_arch(name, directory=None):
    return _load_arch(name, _ISA_DIR if directory is None else directory)


class SimpleISATest(unittest.TestCase):
    def test_populated_sources(self):
        self.assertEqual(len(available()), 23)
        self.assertTrue({"x86", "aarch64", "riscv64", "wasm", "spirv", "nvptx"}
                        <= set(available()))
        for name in available():
            self.assertTrue(load_arch(name).operations)

    def test_other_fixed_width_targets(self):
        cases = [
            ("arm32", "add", dict(rd="r0", rs1="r1", rs2="r2"), "020081e0"),
            ("avr", "add", dict(rd="r1", rs1="r2"), "120c"),
            ("bpf", "add64", dict(rd="r0", rs1="r1"), "0f10000000000000"),
            ("loongarch32", "add_w", dict(rd="r4", rs1="r5", rs2="r6"), "a4181000"),
            ("mips32", "addu", dict(rd="t0", rs1="t1", rs2="t2"), "012a4021"),
            ("mips64", "addu", dict(rd="t0", rs1="t1", rs2="t2"), "01ae6021"),
            ("ppc32", "add", dict(rd="r3", rs1="r4", rs2="r5"), "7c642a14"),
            ("riscv32", "add", dict(rd="a0", rs1="a1", rs2="a2"), "3385c500"),
            ("s390x", "ar", dict(rd="r1", rs1="r2"), "1a12"),
            ("sparc64", "add", dict(rd="g3", rs1="g1", rs2="g2"), "86004002"),
            ("amdgpu", "s_add_u32", dict(rd="s0", rs1="s1", rs2="s2"), "01020080"),
            ("hexagon", "add", dict(rd="r0", rs1="r1", rs2="r2"), "00c201f3"),
            ("m68k", "move_l", dict(rd="d0", rs1="d1"), "2001"),
        ]
        for arch, name, operands, expected in cases:
            with self.subTest(arch=arch):
                self.assertEqual(load_arch(arch).encode(name, **operands).hex(), expected)
        self.assertEqual(load_arch("ppc32").render("add", rd="r3", rs1="r4", rs2="r5"),
                         "add 3, 4, 5")
        with self.assertRaises(ISAError):
            load_arch("m68k").encode("move_l", rd="a0", rs1="d1")
        with self.assertRaises(ISAError):
            load_arch("nvptx").render("add_u32", rd="rd0", rs1="r1", rs2="r2")

    def test_streams_and_text_only(self):
        wasm = load_arch("wasm")
        self.assertEqual(wasm.encode("i32_const", imm=624485).hex(), "41e58e26")
        self.assertEqual(wasm.encode("i32_const", imm=-2).hex(), "417e")
        self.assertEqual(wasm.encode("local_get", index=128).hex(), "208001")
        with self.assertRaises(ISAError):
            wasm.encode("i32_const", imm=1 << 31)
        spirv = load_arch("spirv")
        self.assertEqual(spirv.encode("iadd", type=3, result=7, lhs=4, rhs=4).hex(),
                         "8000050003000000070000000400000004000000")
        for arch in ("nvptx", "directx", "tricore"):
            isa = load_arch(arch)
            name = next(iter(isa.operations))
            op = isa.operation(name)
            operands = {n: next(r.name for r in isa.registers.values() if r.regclass == kind)
                        if kind in ("GPR", "FPR") else 0 for n, kind in op.operands}
            self.assertTrue(isa.render(name, **operands))
            with self.assertRaises(ISAError):
                isa.encode(name, **operands)

    def test_riscv64(self):
        isa = load_arch("riscv64")
        samples = [
            ("add", dict(rd="a0", rs1="a1", rs2="a2"), "3385c500"),
            ("sd", dict(rs2="a0", rs1="sp", imm=8), "2334a100"),
            ("beq", dict(rs1="ra", rs2="sp", offset=-4), "e38e20fe"),
            ("lui", dict(rd="a0", upper=1), "37150000"),
        ]
        for op, args, expected in samples:
            with self.subTest(op=op):
                self.assertEqual(isa.encode(op, **args).hex(), expected)
        self.assertEqual(isa.render("add", rd="a0", rs1="a1", rs2="a2"),
                         "add a0, a1, a2")
        self.assertEqual(isa.register("fp"), isa.register("x8"))
        self.assertEqual(isa.encode("fadd_s", rd="f0", rs1="f1", rs2="f2").hex(),
                         "53802000")
        with self.assertRaises(ISAError):
            isa.encode("addi", rd="a0", rs1="a0", imm=2048)
        with self.assertRaises(ISAError):
            isa.encode("beq", rs1="x1", rs2="x2", offset=3)

    def test_aarch64(self):
        isa = load_arch("aarch64")
        self.assertEqual(isa.encode("add", rd="x0", rs1="x1", rs2="x2").hex(), "2000028b")
        self.assertEqual(isa.encode("ldr", rd="x0", rs1="x1", offset=16).hex(), "200840f9")
        self.assertEqual(isa.encode("ret", rs1="lr").hex(), "c0035fd6")
        self.assertEqual(isa.encode("mul", rd="x0", rs1="x1", rs2="x2").hex(), "207c029b")
        self.assertEqual(isa.encode("addi", rd="sp", rs1="sp", imm=16).hex(), "ff430091")
        with self.assertRaises(ISAError):
            isa.encode("ldr", rd="x0", rs1="x1", offset=7)
        with self.assertRaises(ISAError):
            isa.encode("add", rd="sp", rs1="x1", rs2="x2")
        with self.assertRaises(ISAError):
            isa.encode("addi", rd="xzr", rs1="x1", imm=1)

    def test_aarch64_backward_branches(self):
        isa = load_arch("aarch64")
        self.assertEqual(isa.encode("b", offset=-4).hex(), "ffffff17")
        self.assertEqual(isa.encode("b_eq", offset=-4).hex(), "e0ffff54")
        self.assertEqual(isa.encode("cbnz", rs1="x0", offset=-4).hex(), "e0ffffb5")
        for offset in (-2, 2, 1 << 27, -(1 << 27)-4):
            with self.assertRaises(ISAError):
                isa.encode("b", offset=offset)

    def test_x86(self):
        isa = load_arch("x86")
        self.assertEqual(isa.encode("add", rd="r8", rs1="r9").hex(), "4d01c8")
        self.assertEqual(isa.encode("imul", rd="rax", rs1="rbx").hex(), "480fafc3")
        self.assertEqual(isa.encode("addss", rd="xmm0", rs1="xmm1").hex(), "f30f58c1")
        self.assertEqual(isa.encode("push", rd="r8").hex(), "4150")
        self.assertEqual(isa.encode("ret").hex(), "c3")
        self.assertEqual(isa.encode("call", offset=-5).hex(), "e8fbffffff")
        with self.assertRaises(ISAError):
            isa.encode("add", rd="rax", rs1="x1")

    def test_amd64_jit_forms(self):
        isa = load_arch("x86")
        self.assertEqual(isa.encode("mov_imm", rd="r9", imm=0x123456789abcdef0).hex(),
                         "49b9f0debc9a78563412")
        self.assertEqual(isa.encode("shift_cl", rd="rax", selector=4).hex(), "48d3e0")
        self.assertEqual(isa.encode("shift_cl", rd="r9", selector=5).hex(), "49d3e9")
        self.assertEqual(isa.encode("unary64", rd="rcx", selector=7).hex(), "48f7f9")
        for selector in (-1, 8):
            with self.assertRaises(ISAError):
                isa.encode("shift_cl", rd="rax", selector=selector)
        branches = "jo jno jb jae je jne jbe ja js jns jp jnp jl jge jle jg".split()
        for condition, op in enumerate(branches):
            self.assertEqual(isa.encode(op, offset=-6), bytes((15, 128+condition, 250, 255, 255, 255)))

    def test_reject_bad_sources(self):
        text = Path(available()["riscv64"]).read_text()
        with self.assertRaises(ISAError):
            parse(text.replace("rs1 = 15:5", "rs1 = 7:5", 1))
        with self.assertRaises(ISAError):
            parse(text.replace("{rd}, {rs1}, {rs2}", "{rd}, {rs1}, {oops}", 1))


if __name__ == "__main__":
    unittest.main()
