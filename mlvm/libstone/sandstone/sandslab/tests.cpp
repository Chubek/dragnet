#include "SandSLAB.hpp"
#include "examples/Forth.hpp"
#include <iostream>
#if defined(__x86_64__) && defined(__unix__)
#include <sys/mman.h>
#endif

using namespace sandslab;

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
template <class F> static void fails(F f) {
    try { f(); } catch (const Error &) { return; }
    throw std::runtime_error("expected an ISA error");
}

int main() {
    try {
        auto rv = isa::load_arch("riscv64", SANDSLAB_TEST_ISA_DIR);
        Program p(rv);
        struct Add : Instr<"add"> { using Instr::Instr; };
        p.emit(Add{{{"rd", "a0"}, {"rs1", "a1"}, {"rs2", "a2"}}});
        require(p.compile().bytes == std::vector<std::uint8_t>({0x33, 0x85, 0xc5, 0}), "RISC-V add");
        require(p.assembly() == "add a0, a1, a2\n", "assembly rendering");
        fails([&] { p.emit("missing"); });
        fails([&] { p.emit("add", {{"rd", "rax"}}); });

        auto x86 = isa::load_arch("x86", SANDSLAB_TEST_ISA_DIR);
        Program branch(x86);
        branch.label("start").emit(Instruction{"jmp", {}},
            {{"offset", "end", RelativeBase::next_instruction}})
            .emit("ret").label("end").emit(Instruction{"jmp", {}},
            {{"offset", "start", RelativeBase::next_instruction}});
        auto code = branch.compile();
        require(code.bytes == std::vector<std::uint8_t>({0xe9, 1, 0, 0, 0, 0xc3, 0xe9, 0xf5, 0xff, 0xff, 0xff}), "forward/backward fixup");
        require(code.labels.at("end") == 6, "label offset");
        require(branch.compile().bytes == code.bytes, "repeatable compilation");
        fails([&] { branch.label("end"); });
        fails([&] { branch.assembly(); });
        fails([&] {
            Program q(x86);
            q.emit(Instruction{"jmp", {}}, {{"offset", "missing"}}).compile();
        });
        fails([&] {
            Program q(x86);
            q.emit(Instruction{"jmp8", {}}, {{"offset", "far", RelativeBase::next_instruction}});
            for (int i = 0; i < 128; ++i) q.emit("ret");
            q.label("far").compile();
        });
        Program text(isa::load_arch("nvptx", SANDSLAB_TEST_ISA_DIR));
        text.emit("ret");
        require(text.assembly() == "ret;\n", "text-only rendering");
        fails([&] { text.compile(); });

        for (auto source : {"", "1 2", "+", "1 swap", "1 unknown", "9223372036854775808", "( open"})
            fails([&] { examples::compile_forth(source, x86); });
        const std::vector<std::pair<std::string, std::int64_t>> cases = {
            {"6 7 *", 42}, {"10 3 -", 7}, {"3 dup * 2 +", 11},
            {"3 10 swap -", 7}, {"3 4 over + *", 21}, {"9 2 drop negate", -9},
            {"-42", -42}, {"-9223372036854775808", INT64_MIN},
            {"6 3 and 8 or 1 xor", 11}, {"( comment ) 4 \\ ignored\n 5 +", 9}
        };
        for (const auto &[source, expected] : cases) {
            auto bytes = examples::compile_forth(source, x86).compile().bytes;
            require(!bytes.empty() && bytes.back() == 0xc3, "Forth return");
#if defined(__x86_64__) && defined(__unix__)
            void *memory = mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            require(memory != MAP_FAILED, "mmap");
            std::copy(bytes.begin(), bytes.end(), static_cast<std::uint8_t *>(memory));
            if (mprotect(memory, bytes.size(), PROT_READ | PROT_EXEC) != 0) {
                munmap(memory, bytes.size());
                throw std::runtime_error("mprotect");
            }
            auto result = reinterpret_cast<std::int64_t (*)()>(memory)();
            munmap(memory, bytes.size());
            require(result == expected, "native Forth result");
#else
            (void)expected;
#endif
        }
        std::cout << "SandSLAB tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
