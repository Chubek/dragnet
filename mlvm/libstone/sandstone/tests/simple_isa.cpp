#include "simple_isa.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace si = qarch::simple_isa;

static std::string hex(const std::vector<std::uint8_t> &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto b : bytes) { out += digits[b >> 4]; out += digits[b & 15]; }
    return out;
}

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Fn> static void fails(Fn &&fn) {
    try { fn(); }
    catch (const si::ISAError &) { return; }
    throw std::runtime_error("expected ISAError");
}

int main() {
    const std::filesystem::path directory = SIMPLE_ISA_TEST_DIR;
    auto paths = si::available(directory);
    require(paths.size() == 23, "wrong number of populated sources");
    for (const auto &[name, path] : paths) {
        auto isa = si::load_file(path);
        if (isa.operations.empty()) throw std::runtime_error(name + " has no forms");
    }

    struct Sample {
        const char *arch, *form;
        si::Operands operands;
        const char *bytes;
    };
    const std::vector<Sample> samples = {
        {"arm32", "add", {{"rd", "r0"}, {"rs1", "r1"}, {"rs2", "r2"}}, "020081e0"},
        {"avr", "add", {{"rd", "r1"}, {"rs1", "r2"}}, "120c"},
        {"bpf", "add64", {{"rd", "r0"}, {"rs1", "r1"}}, "0f10000000000000"},
        {"loongarch32", "add_w", {{"rd", "r4"}, {"rs1", "r5"}, {"rs2", "r6"}}, "a4181000"},
        {"mips32", "addu", {{"rd", "t0"}, {"rs1", "t1"}, {"rs2", "t2"}}, "012a4021"},
        {"mips64", "addu", {{"rd", "t0"}, {"rs1", "t1"}, {"rs2", "t2"}}, "01ae6021"},
        {"ppc32", "add", {{"rd", "r3"}, {"rs1", "r4"}, {"rs2", "r5"}}, "7c642a14"},
        {"riscv32", "add", {{"rd", "a0"}, {"rs1", "a1"}, {"rs2", "a2"}}, "3385c500"},
        {"s390x", "ar", {{"rd", "r1"}, {"rs1", "r2"}}, "1a12"},
        {"sparc64", "add", {{"rd", "g3"}, {"rs1", "g1"}, {"rs2", "g2"}}, "86004002"},
        {"amdgpu", "s_add_u32", {{"rd", "s0"}, {"rs1", "s1"}, {"rs2", "s2"}}, "01020080"},
        {"hexagon", "add", {{"rd", "r0"}, {"rs1", "r1"}, {"rs2", "r2"}}, "00c201f3"},
        {"m68k", "move_l", {{"rd", "d0"}, {"rs1", "d1"}}, "2001"},
        {"riscv64", "beq", {{"rs1", "ra"}, {"rs2", "sp"}, {"offset", -4}}, "e38e20fe"},
        {"riscv64", "lui", {{"rd", "a0"}, {"upper", 1}}, "37150000"},
        {"aarch64", "b", {{"offset", -4}}, "ffffff17"},
        {"aarch64", "b_eq", {{"offset", -4}}, "e0ffff54"},
        {"aarch64", "cbnz", {{"rs1", "x0"}, {"offset", -4}}, "e0ffffb5"},
        {"aarch64", "ldr", {{"rd", "x0"}, {"rs1", "x1"}, {"offset", 16}}, "200840f9"},
        {"aarch64", "addi", {{"rd", "sp"}, {"rs1", "sp"}, {"imm", 16}}, "ff430091"},
        {"x86", "add", {{"rd", "r8"}, {"rs1", "r9"}}, "4d01c8"},
        {"x86", "addss", {{"rd", "xmm0"}, {"rs1", "xmm1"}}, "f30f58c1"},
        {"x86", "call", {{"offset", -5}}, "e8fbffffff"},
        {"x86", "jne", {{"offset", -6}}, "0f85faffffff"},
        {"x86", "mov_imm", {{"rd", "r9"}, {"imm", 0x123456789abcdef0LL}}, "49b9f0debc9a78563412"},
        {"x86", "shift_cl", {{"rd", "rax"}, {"selector", 4}}, "48d3e0"},
        {"x86", "shift_cl", {{"rd", "r9"}, {"selector", 5}}, "49d3e9"},
        {"x86", "unary64", {{"rd", "rcx"}, {"selector", 7}}, "48f7f9"},
        {"wasm", "i32_const", {{"imm", -2}}, "417e"},
        {"wasm", "i32_const", {{"imm", 624485}}, "41e58e26"},
        {"spirv", "iadd", {{"type", 3}, {"result", 7}, {"lhs", 4}, {"rhs", 4}},
         "8000050003000000070000000400000004000000"},
    };
    for (const auto &s : samples) {
        auto actual = hex(si::load_arch(s.arch, directory).encode(s.form, s.operands));
        if (actual != s.bytes)
            throw std::runtime_error(std::string(s.arch) + "." + s.form + ": expected " + s.bytes +
                                     ", got " + actual);
    }
    const auto rv = si::load_arch("riscv64", directory);
    require(rv.render("add", {{"rd", "a0"}, {"rs1", "a1"}, {"rs2", "a2"}}) == "add a0, a1, a2", "RISC-V rendering");
    require(rv.reg("fp").number == rv.reg("x8").number, "RISC-V alias");
    fails([&] { rv.encode("addi", {{"rd", "a0"}, {"rs1", "a0"}, {"imm", 2048}}); });
    fails([&] { rv.encode("beq", {{"rs1", "x1"}, {"rs2", "x2"}, {"offset", 3}}); });
    const auto arm = si::load_arch("aarch64", directory);
    fails([&] { arm.encode("ldr", {{"rd", "x0"}, {"rs1", "x1"}, {"offset", 7}}); });
    fails([&] { arm.encode("add", {{"rd", "sp"}, {"rs1", "x1"}, {"rs2", "x2"}}); });
    require(si::load_arch("ppc32", directory).render("add", {{"rd", "r3"}, {"rs1", "r4"}, {"rs2", "r5"}})
            == "add 3, 4, 5", "PowerPC rendering");
    fails([&] { si::load_arch("wasm", directory).encode("i32_const", {{"imm", std::int64_t{1} << 31}}); });
    const auto ptx = si::load_arch("nvptx", directory);
    require(ptx.render("ret") == "ret;", "PTX rendering");
    fails([&] { ptx.encode("ret"); });

    // Native C++ construction goes through exactly the same validation as a file.
    auto native = si::SourceDSL{}
        .architecture("demo", 32, 32, "little", 4)
        .reg("r0", "GPR", 32, 0).reg("r1", "GPR", 32, 1)
        .alias("zero", "r0")
        .encoding(si::Encoding{"ADD", si::Width::bits32, 0x33, {{"rd", 7, 5, 0}, {"rs1", 15, 5, 0}}, {}})
        .form(si::Operation{"add", "arithmetic", "ADD", "add {rd}, {rs1}",
                            {{"rd", "GPR"}, {"rs1", "GPR"}}})
        .finish();
    require(hex(native.encode("add", {{"rd", "zero"}, {"rs1", "r1"}})) == "33800000", "native DSL encoding");

    std::ifstream rv_file(paths.at("riscv64"));
    const auto source = std::string(std::istreambuf_iterator<char>(rv_file), {});
    fails([&] {
        auto bad = source;
        bad.replace(bad.find("rs1 = 15:5"), std::string("rs1 = 15:5").size(), "rs1 = 7:5");
        si::parse(bad);
    });
    fails([&] {
        auto bad = source;
        bad.replace(bad.find("{rd}, {rs1}, {rs2}"), std::string("{rd}, {rs1}, {rs2}").size(), "{rd}, {rs1}, {oops}");
        si::parse(bad);
    });
    fails([] {
        si::SourceDSL{}
            .architecture("amd64", 64, 64, "little", 1)
            .encoding(si::Encoding{"BAD", si::Width::variable, 0, {},
                                   {{"opcode", "0x01"}, {"modrm", "missing,rd"}}})
            .form(si::Operation{"bad", "arithmetic", "BAD", "bad {rd}", {{"rd", "GPR"}}})
            .finish();
    });
    auto amd64 = si::load_file(paths.at("x86"));
    fails([&] { amd64.encode("shift_cl", {{"rd", "rax"}, {"selector", -1}}); });
    fails([&] { amd64.encode("shift_cl", {{"rd", "rax"}, {"selector", 8}}); });
    std::cout << "23 architectures loaded; " << samples.size() << " golden encodings passed\n";
}
