#pragma once
#include <mlvm/core.hpp>
#include <span>
#include <set>

namespace mlvm {
enum class Endianness { little, big };
struct Target {
    std::string triple;
    std::uint16_t machine;
    unsigned pointer_bits;
    Endianness endianness;
    std::string object_format;
};
class TargetRegistry {
public:
    bool add(Target target);
    [[nodiscard]] const Target* find(std::string_view triple) const;
    static TargetRegistry builtins();
private:
    std::map<std::string, Target, std::less<>> targets_;
};
struct Section {
    std::string name;
    std::uint64_t flags = 0, alignment = 1;
    std::vector<std::uint8_t> data;
};
enum class Binding : std::uint8_t { local = 0, global = 1, weak = 2 };
struct Symbol {
    std::string name;
    Binding binding = Binding::local;
    std::uint8_t type = 0;
    // -1 denotes undefined, -2 absolute; otherwise a zero-based section index.
    std::int32_t section = -1;
    std::uint64_t value = 0, size = 0;
};
struct Relocation {
    std::size_t section = 0, symbol = 0;
    std::uint64_t offset = 0;
    std::uint32_t type = 0; // x86-64: 1 = absolute64, 2 = PC32, 4 = PLT32.
    std::int64_t addend = 0;
};
struct Object {
    std::uint16_t machine = 62;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
};
bool verify_object(const Object& object, DiagnosticEngine& diagnostics);
std::optional<Object> read_elf(std::span<const std::uint8_t> bytes, DiagnosticEngine& diagnostics);
std::optional<std::vector<std::uint8_t>> write_elf(const Object& object, DiagnosticEngine& diagnostics);
std::optional<Object> assemble(std::string_view text, SourceManager& sources, Identifier file, DiagnosticEngine& diagnostics);
std::optional<Object> link_relocatable(std::span<const Object> objects, DiagnosticEngine& diagnostics);
std::optional<std::vector<std::uint8_t>> link_executable(std::span<const Object> objects, std::string_view entry, DiagnosticEngine& diagnostics);
struct ArchiveMember { std::string name; std::vector<std::uint8_t> data; };
std::optional<std::vector<ArchiveMember>> read_archive(std::span<const std::uint8_t> bytes, DiagnosticEngine& diagnostics);
std::optional<std::vector<std::uint8_t>> write_archive(std::span<const ArchiveMember> members, DiagnosticEngine& diagnostics);
} // namespace mlvm
