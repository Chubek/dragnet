#pragma once

#include "SandSLAB.hpp"
#include <charconv>

namespace sandslab::examples {

// Compile a closed expression to an AMD64 leaf function returning int64 in RAX.
// Only caller-saved registers are used. Every pushed cell is popped before ret.
inline Program compile_forth(std::string_view source, isa::ISA target) {
    if (target.name != "amd64") throw Error("FORTHCC requires the AMD64 ISA (x86.isa)");
    Program program(std::move(target));
    std::istringstream input{std::string(source)};
    std::string word;
    std::size_t depth = 0, index = 0;
    auto push = [&](const char *reg) { program.emit("push", {{"rd", reg}}); };
    auto pop = [&](const char *reg) { program.emit("pop", {{"rd", reg}}); };
    while (input >> word) {
        ++index;
        if (word == "\\") { std::getline(input, word); continue; }
        if (word == "(") {
            bool closed = false;
            while (input >> word) if (word == ")") { closed = true; break; }
            if (!closed) throw Error("unterminated Forth comment");
            continue;
        }
        auto need = [&](std::size_t n) {
            if (depth < n) throw Error("stack underflow at word " + std::to_string(index) + ": " + word);
        };
        std::int64_t value{};
        auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
        if (error == std::errc{} && end == word.data() + word.size()) {
            program.emit("mov_imm", {{"rd", "rax"}, {"imm", value}});
            push("rax");
            ++depth;
        } else if (word == "+" || word == "-" || word == "*" || word == "and" || word == "or" || word == "xor") {
            need(2);
            pop("rcx"); pop("rax");
            const auto op = word == "+" ? "add" : word == "-" ? "sub" : word == "*" ? "imul" : word;
            program.emit(op, {{"rd", "rax"}, {"rs1", "rcx"}});
            push("rax");
            --depth;
        } else if (word == "dup") {
            need(1); pop("rax"); push("rax"); push("rax"); ++depth;
        } else if (word == "drop") {
            need(1); pop("rax"); --depth;
        } else if (word == "swap") {
            need(2); pop("rcx"); pop("rax"); push("rcx"); push("rax");
        } else if (word == "over") {
            need(2); pop("rcx"); pop("rax"); push("rax"); push("rcx"); push("rax"); ++depth;
        } else if (word == "negate") {
            need(1); pop("rcx");
            program.emit("xor", {{"rd", "rax"}, {"rs1", "rax"}});
            program.emit("sub", {{"rd", "rax"}, {"rs1", "rcx"}});
            push("rax");
        } else {
            throw Error("unknown word or invalid int64 literal: " + word);
        }
    }
    if (depth != 1) throw Error("Forth expression must leave exactly one stack cell; got " + std::to_string(depth));
    pop("rax");
    program.emit(Instr<"ret">{});
    return program;
}
} // namespace sandslab::examples
