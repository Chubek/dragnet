#include "test.hpp"
#include <mlvm/object.hpp>
#include <array>

int main() { return test_main([] {
    mlvm::SourceManager sources; mlvm::DiagnosticEngine diagnostics;
    auto assemble = [&](std::string source) { return mlvm::assemble(source, sources, sources.add("test.s", source), diagnostics); };
    auto caller = assemble(".globl _start\n_start:\ncall answer\nmov $60, %eax\nmov $42, %edi\nsyscall\n.data\n.quad answer\n");
    auto callee = assemble(".globl answer\n.type answer, @function\nanswer:\nmov $42, %eax\nret\n.size answer, .-answer\n");
    CHECK(caller); CHECK(callee); CHECK(caller->relocations.size() == 2);
    auto encoded = mlvm::write_elf(*caller, diagnostics); CHECK(encoded);
    auto decoded = mlvm::read_elf(*encoded, diagnostics); CHECK(decoded);
    auto reencoded = mlvm::write_elf(*decoded, diagnostics); CHECK(reencoded); CHECK(*encoded == *reencoded);
    CHECK(decoded->relocations[0].addend == -4);
    // Every proper prefix of our object is malformed; bounds checks must reject all.
    for (std::size_t size = 0; size < encoded->size(); ++size) {
        CHECK(!mlvm::read_elf(std::span(encoded->data(), size), diagnostics)); diagnostics.clear();
    }
    auto bad = *encoded; for (unsigned i = 40; i < 48; ++i) bad[i] = 255;
    CHECK(!mlvm::read_elf(bad, diagnostics)); diagnostics.clear();
    bad = *encoded; bad[18] = 0; CHECK(!mlvm::read_elf(bad, diagnostics)); diagnostics.clear();
    // Many symbols referencing one long string are structurally in bounds but
    // must not cause unbounded allocation amplification.
    mlvm::Object repeated;
    repeated.symbols.push_back({std::string(1024, 'X'), mlvm::Binding::global, 0, -2, 0, 0});
    for (unsigned i = 0; i < 100; ++i) repeated.symbols.push_back({"s" + std::to_string(i), mlvm::Binding::global, 0, -2, 0, 0});
    auto repeated_bytes = mlvm::write_elf(repeated, diagnostics); CHECK(repeated_bytes);
    auto little = [&](std::size_t offset) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(repeated_bytes->at(offset + i)) << (8 * i);
        return static_cast<std::size_t>(value);
    };
    const auto section_headers = little(40);
    const auto symbol_offset = little(section_headers + 64 + 24), symbol_size = little(section_headers + 64 + 32);
    for (std::size_t p = symbol_offset + 24; p < symbol_offset + symbol_size; p += 24) {
        repeated_bytes->at(p) = 1;
        for (unsigned i = 1; i < 4; ++i) repeated_bytes->at(p + i) = 0;
    }
    CHECK(!mlvm::read_elf(*repeated_bytes, diagnostics));
    CHECK(diagnostics.diagnostics().back().message.find("budget") != std::string::npos); diagnostics.clear();
    const std::array objects{*caller, *callee};
    auto linked = mlvm::link_relocatable(objects, diagnostics); CHECK(linked);
    CHECK(mlvm::write_elf(*linked, diagnostics));
    auto executable = mlvm::link_executable(objects, "_start", diagnostics); CHECK(executable); CHECK((*executable)[16] == 2);
    CHECK(!mlvm::link_executable(std::span(&*caller, 1), "_start", diagnostics)); diagnostics.clear();
    const std::array duplicate{*callee, *callee}; CHECK(!mlvm::link_relocatable(duplicate, diagnostics)); diagnostics.clear();
    auto weak = *callee; weak.symbols[0].binding = mlvm::Binding::weak;
    const std::array weak_objects{weak, *callee}; CHECK(mlvm::link_relocatable(weak_objects, diagnostics));
    auto overflow = *callee; overflow.symbols[0].section = -2; overflow.symbols[0].value = UINT64_MAX;
    const std::array overflowing{*caller, overflow}; CHECK(!mlvm::link_executable(overflowing, "_start", diagnostics)); diagnostics.clear();
    // The relocation field is at VA 0x401001. Check both signed limits,
    // both directions of overflow, and cancellation with a negative addend.
    auto boundary_caller = *caller; boundary_caller.relocations.resize(1);
    boundary_caller.relocations[0].addend = 0;
    auto absolute = *callee; absolute.symbols[0].section = -2;
    for (auto extra : {std::uint64_t{INT32_MAX}, std::uint64_t{INT32_MAX} + 1}) {
        absolute.symbols[0].value = 0x401001 + extra;
        const std::array input{boundary_caller, absolute};
        CHECK(bool(mlvm::link_executable(input, "_start", diagnostics)) == (extra == INT32_MAX)); diagnostics.clear();
    }
    absolute.symbols[0].value = 0;
    boundary_caller.relocations[0].addend = -std::int64_t{2147483648} + 0x401001;
    { const std::array input{boundary_caller, absolute}; CHECK(mlvm::link_executable(input, "_start", diagnostics)); }
    --boundary_caller.relocations[0].addend;
    { const std::array input{boundary_caller, absolute}; CHECK(!mlvm::link_executable(input, "_start", diagnostics)); diagnostics.clear(); }
    auto invalid = *caller; invalid.relocations[0].offset = UINT64_MAX;
    CHECK(!mlvm::write_elf(invalid, diagnostics)); diagnostics.clear();
    CHECK(!assemble(".text\nmadeup %rax\n")); diagnostics.clear();
    CHECK(!assemble(".byte 256\n")); diagnostics.clear();
    CHECK(!assemble(".balign 3\n")); diagnostics.clear();
    CHECK(!assemble("duplicate:\nret\nduplicate:\nret\n")); diagnostics.clear();
    auto extended = assemble(".globl f\nf:\nadd $1, %eax\nsub $2, %ebx\npush %ebp\npop %ebp\nret\n");
    CHECK(extended);
    auto extended_bytes = mlvm::write_elf(*extended, diagnostics); CHECK(extended_bytes);
    auto extended_back = mlvm::read_elf(*extended_bytes, diagnostics); CHECK(extended_back);
    CHECK(extended_back->sections[0].data == extended->sections[0].data);
    CHECK(!assemble("add %eax, %ebx\n")); diagnostics.clear();
    CHECK(!assemble("push %rax\n")); diagnostics.clear();
    const std::array members{mlvm::ArchiveMember{"one.o", *encoded}, mlvm::ArchiveMember{"odd", {1, 2, 3}}};
    auto archive = mlvm::write_archive(members, diagnostics); CHECK(archive);
    auto extracted = mlvm::read_archive(*archive, diagnostics); CHECK(extracted); CHECK(extracted->size() == 2); CHECK((*extracted)[0].data == *encoded);
    archive->pop_back(); CHECK(!mlvm::read_archive(*archive, diagnostics)); diagnostics.clear();
    const std::array unsafe{mlvm::ArchiveMember{"../bad", {}}}; CHECK(!mlvm::write_archive(unsafe, diagnostics));
    auto targets = mlvm::TargetRegistry::builtins(); CHECK(targets.find("x86_64-unknown-linux-gnu")); CHECK(!targets.find("guessed"));
}); }
