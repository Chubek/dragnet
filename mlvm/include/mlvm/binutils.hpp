#pragma once
#include <mlvm/object.hpp>
#include <functional>

namespace mlvm {
// Owning, format-neutral views used by the binary tools. All section/symbol
// references are zero-based. No libstone implementation types cross this API.
struct BinaryFormat {
    unsigned bits = 64;
    std::uint16_t machine = 62;
    Endianness endianness = Endianness::little;
    std::uint32_t flags = 0;
    std::uint8_t osabi = 0, abi_version = 0;
};
struct BinarySection {
    std::string name;
    std::uint32_t type = 1;
    std::uint64_t flags = 0, alignment = 1, entry_size = 0, zero_fill = 0;
    std::vector<std::uint8_t> data;
    [[nodiscard]] std::uint64_t size() const { return type == 8 ? zero_fill : data.size(); }
};
struct BinarySymbol {
    std::string name;
    std::int32_t section = -1; // -1 undefined; -2 absolute.
    std::uint64_t value = 0, size = 0;
    Binding binding = Binding::local;
    std::uint8_t type = 0, visibility = 0;
};
struct BinaryRelocation {
    std::size_t section = 0, symbol = 0;
    std::uint32_t type = 0;
    std::uint64_t offset = 0;
    std::int64_t addend = 0;
    bool implicit_addend = false;
};
struct BinaryGroupMember {
    enum class Kind { content, rela, rel } kind = Kind::content;
    std::size_t section = 0;
};
struct BinaryGroup {
    std::string name = ".group";
    std::size_t signature = 0;
    std::uint32_t flags = 1;
    std::vector<BinaryGroupMember> members;
};
struct BinaryObject {
    BinaryFormat format;
    std::vector<BinarySection> sections;
    std::vector<BinarySymbol> symbols;
    std::vector<BinaryRelocation> relocations;
    std::vector<BinaryGroup> groups;
};
struct BinaryArchiveMember { std::string name; BinaryObject object; };
using BinaryArchive = std::vector<BinaryArchiveMember>;

// ELF32/ELF64 ET_REL, either byte order; regular GNU/BSD ELF archives.
// Input, expanded content and output are bounded to 256 MiB; upstream count
// limits also apply. Archives are canonically serialized with a GNU index.
std::optional<BinaryObject> read_binary_object(std::span<const std::uint8_t>, DiagnosticEngine&);
std::optional<std::vector<std::uint8_t>> write_binary_object(const BinaryObject&, DiagnosticEngine&);
std::optional<BinaryArchive> read_binary_archive(std::span<const std::uint8_t>, DiagnosticEngine&);
std::optional<std::vector<std::uint8_t>> write_binary_archive(const BinaryArchive&, DiagnosticEngine&);
// Preserve arbitrary member bytes. ELF members contribute symbols to the GNU
// index through libstone; other members have no symbol-index entries.
std::optional<std::vector<std::uint8_t>> write_indexed_archive(std::span<const ArchiveMember>, DiagnosticEngine&);
bool verify_binary_object(const BinaryObject&, DiagnosticEngine&);

enum class StripPolicy { none, locals, debug, all };
struct BinaryTransform {
    std::set<std::string> remove_sections, only_sections;
    std::map<std::string, std::string> rename_sections;
    StripPolicy strip = StripPolicy::none;
};
// Transactional: failure preserves the original object. Retained relocation
// and group references must remain valid; all-strip rejects these references.
bool transform_binary_object(BinaryObject&, const BinaryTransform&, DiagnosticEngine&);

struct RelocationEncoding {
    std::uint32_t type = 0;
    std::string name;
    bool pc_relative = false, signed_value = false;
    std::uint32_t storage_bytes = 0, bit_offset = 0, bits = 0;
    std::uint64_t scale = 1;
    std::int64_t pc_bias = 0;
    std::optional<bool> implicit_addend_signed;
};
struct LinkContract {
    BinaryFormat format;
    std::uint64_t text_alignment = 1;
    std::vector<RelocationEncoding> relocations;
};
// Strict, version-1 TOML contract; see docs/TOOLS.md. Target facts are explicit.
std::optional<LinkContract> parse_link_contract(std::string_view, DiagnosticEngine&);
struct RawLinkOptions {
    std::uint64_t base_address = 0;
    std::map<std::string, std::uint64_t> externals;
};
struct BinaryPlacement {
    std::size_t object = 0, section = 0;
    std::string name;
    std::uint64_t address = 0, offset = 0, size = 0, flags = 0;
};
struct BinaryImage {
    std::vector<std::uint8_t> data;
    std::vector<BinaryPlacement> sections;
    std::map<std::string, std::uint64_t> symbols;
    std::vector<std::string> discarded_groups;
};
// Archives are rescanned to a fixed point after roots; the image owns bytes,
// not executable memory. All relocation semantics come from the contract.
std::optional<BinaryImage> link_binary_objects(std::span<const BinaryObject>,
    std::span<const BinaryArchive>, const LinkContract&, const RawLinkOptions&, DiagnosticEngine&);

enum class StringEncoding { ascii, utf16le_ascii, utf16be_ascii };
struct StringOptions { std::size_t minimum = 4; StringEncoding encoding = StringEncoding::ascii; };
// Callback views are borrowed only during the call. UTF-16 modes recognize
// printable ASCII code points in aligned two-byte units, not general Unicode.
bool scan_binary_strings(std::span<const std::uint8_t>, const StringOptions&,
    const std::function<void(std::uint64_t, std::string_view)>&, DiagnosticEngine&);
} // namespace mlvm
