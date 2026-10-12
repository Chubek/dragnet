#include "adapter.hpp"
#include <limits>
#include <stdexcept>

namespace mlvm::detail {
stone::ObjectLimits object_limits() { stone::ObjectLimits result; result.bytes = binary_byte_limit; return result; }
stone::ArchiveLimits archive_limits() {
    stone::ArchiveLimits result; result.bytes = binary_byte_limit; result.object = object_limits(); return result;
}
namespace {
struct InputBudget {
    std::uint64_t bytes = 0;
    std::size_t objects = 0, sections = 0, symbols = 0, relocations = 0, groups = 0;
    void account(std::uint64_t size) {
        if (size > binary_byte_limit - bytes) throw std::invalid_argument("binary expanded byte budget exceeded");
        bytes += size;
    }
    void add(const BinaryObject& value) {
        const auto limits = object_limits();
        if (objects >= 16384 || value.sections.size() > limits.sections - sections || value.symbols.size() > limits.symbols - symbols ||
            value.relocations.size() > limits.relocations - relocations || value.groups.size() > limits.groups - groups)
            throw std::invalid_argument("binary aggregate count budget exceeded");
        ++objects; sections += value.sections.size(); symbols += value.symbols.size(); relocations += value.relocations.size(); groups += value.groups.size();
        for (const auto& s : value.sections) { account(s.size()); account(s.name.size()); }
        for (const auto& s : value.symbols) account(s.name.size());
        std::size_t members = 0;
        for (const auto& g : value.groups) {
            if (g.members.size() > value.sections.size() * 3 - members) throw std::invalid_argument("binary group-member count budget exceeded");
            members += g.members.size(); account(g.name.size());
        }
    }
};
std::uint32_t index(std::size_t value) {
    if (value >= stone::object_absolute) throw std::invalid_argument("binary index exceeds supported range");
    return static_cast<std::uint32_t>(value);
}
stone::ObjectFormat format(const BinaryFormat& value) {
    if (value.bits != 32 && value.bits != 64) throw std::invalid_argument("ELF class must be 32 or 64");
    if (value.endianness != Endianness::little && value.endianness != Endianness::big)
        throw std::invalid_argument("invalid binary byte order");
    return {value.machine, value.endianness == Endianness::little ? stone::ByteOrder::Little : stone::ByteOrder::Big,
        value.flags, value.osabi, value.abi_version, value.bits == 32 ? stone::ElfClass::Elf32 : stone::ElfClass::Elf64};
}
}
stone::ObjectFile to_stone(const BinaryObject& value) {
    InputBudget budget; budget.add(value);
    stone::ObjectFile result; result.format = format(value.format);
    for (const auto& s : value.sections)
        result.sections.push_back({s.name, s.type, s.flags, s.alignment, s.entry_size, s.data, s.zero_fill});
    for (const auto& s : value.symbols) {
        if (s.section < -2) throw std::invalid_argument("invalid binary symbol section");
        result.symbols.push_back({s.name, s.section == -1 ? stone::object_undefined : s.section == -2 ? stone::object_absolute : index(static_cast<std::size_t>(s.section)),
            s.value, s.size, static_cast<stone::SymbolBinding>(s.binding), s.type, s.visibility});
    }
    for (const auto& r : value.relocations)
        result.relocations.push_back({index(r.section), index(r.symbol), r.type, r.offset, r.addend, r.implicit_addend});
    for (const auto& g : value.groups) {
        stone::ObjectGroup group; group.name = g.name; group.signature = index(g.signature); group.flags = g.flags;
        for (const auto& m : g.members) group.members.push_back({static_cast<stone::ObjectGroupMember::Kind>(m.kind), index(m.section)});
        result.groups.push_back(std::move(group));
    }
    return result;
}
BinaryObject from_stone(const stone::ObjectFile& value) {
    BinaryObject result;
    const auto& f = value.format;
    result.format = {f.elf_class == stone::ElfClass::Elf32 ? 32U : 64U, f.machine,
        f.byte_order == stone::ByteOrder::Little ? Endianness::little : Endianness::big, f.flags, f.osabi, f.abi_version};
    for (const auto& s : value.sections)
        result.sections.push_back({s.name, s.type, s.flags, s.alignment, s.entry_size, s.zero_fill, s.bytes});
    for (const auto& s : value.symbols)
        result.symbols.push_back({s.name, s.section == stone::object_undefined ? -1 : s.section == stone::object_absolute ? -2 : static_cast<std::int32_t>(s.section),
            s.value, s.size, static_cast<Binding>(s.binding), s.type, s.visibility});
    for (const auto& r : value.relocations)
        result.relocations.push_back({r.section, r.symbol, r.type, r.offset, r.addend, r.implicit_addend});
    for (const auto& g : value.groups) {
        BinaryGroup group; group.name = g.name; group.signature = g.signature; group.flags = g.flags;
        for (const auto& m : g.members) group.members.push_back({static_cast<BinaryGroupMember::Kind>(m.kind), m.section});
        result.groups.push_back(std::move(group));
    }
    return result;
}
stone::ObjectTarget to_stone(const LinkContract& value) {
    if (value.relocations.size() > 4096) throw std::invalid_argument("target relocation count exceeds 4096");
    stone::ObjectTarget result; result.format = format(value.format); result.text_alignment = value.text_alignment;
    for (const auto& r : value.relocations) {
        stone::RelocationType type; type.type = r.type; type.name = r.name;
        type.kind = r.pc_relative ? stone::RelocationKind::PCRelative : stone::RelocationKind::Absolute;
        type.storage_bytes = r.storage_bytes; type.bit_offset = r.bit_offset; type.bits = r.bits;
        type.scale = r.scale; type.signed_value = r.signed_value; type.pc_bias = r.pc_bias;
        type.implicit_addend_signed = r.implicit_addend_signed;
        if (!result.relocations.emplace(r.type, std::move(type)).second) throw std::invalid_argument("duplicate relocation type");
    }
    return result;
}
} // namespace mlvm::detail

namespace mlvm {
namespace {
template<class T> std::optional<T> failure(DiagnosticEngine& diagnostics, std::string_view code, const std::string& message) {
    diagnostics.error(std::string(code), message); return std::nullopt;
}
}
std::optional<BinaryObject> read_binary_object(std::span<const std::uint8_t> bytes, DiagnosticEngine& diagnostics) {
    auto result = detail::stone::load_elf(bytes, {}, detail::object_limits());
    if (!result) return failure<BinaryObject>(diagnostics, "binary.read", result.error().message);
    return detail::from_stone(result.value());
}
std::optional<std::vector<std::uint8_t>> write_binary_object(const BinaryObject& object, DiagnosticEngine& diagnostics) {
    try {
        auto result = detail::stone::emit_elf(detail::to_stone(object), detail::object_limits());
        if (!result) return failure<std::vector<std::uint8_t>>(diagnostics, "binary.write", result.error().message);
        return std::move(result.value());
    } catch (const std::invalid_argument& error) { return failure<std::vector<std::uint8_t>>(diagnostics, "binary.write", error.what()); }
}
bool verify_binary_object(const BinaryObject& object, DiagnosticEngine& diagnostics) {
    try {
        auto result = detail::stone::validate(detail::to_stone(object), detail::object_limits());
        if (result) return true;
        diagnostics.error("binary.invalid", result.error().message);
    } catch (const std::invalid_argument& error) { diagnostics.error("binary.invalid", error.what()); }
    return false;
}
std::optional<BinaryArchive> read_binary_archive(std::span<const std::uint8_t> bytes, DiagnosticEngine& diagnostics) {
    // Frame members with MLVM's byte-preserving reader, then decrease upstream
    // budgets before each ELF decode. A corrupt archive cannot multiply the
    // per-object name/NOBITS/count budgets by its member count.
    auto members = read_archive(bytes, diagnostics); if (!members) return std::nullopt;
    auto limits = detail::object_limits(); BinaryArchive archive;
    std::uint64_t expanded = 0;
    for (const auto& m : *members) {
        auto account = [&](std::uint64_t size) {
            if (size > detail::binary_byte_limit - expanded) return false;
            expanded += size; return true;
        };
        if (!account(m.name.size())) return failure<BinaryArchive>(diagnostics, "binary.archive.read", "archive expanded byte budget exceeded");
        limits.bytes = detail::binary_byte_limit - expanded;
        auto decoded = detail::stone::load_elf(m.data, m.name, limits);
        if (!decoded) return failure<BinaryArchive>(diagnostics, "binary.archive.read", decoded.error().message);
        const auto& object = decoded.value();
        for (const auto& s : object.sections) if (!account(s.size()) || !account(s.name.size()))
            return failure<BinaryArchive>(diagnostics, "binary.archive.read", "archive expanded byte budget exceeded");
        for (const auto& s : object.symbols) if (!account(s.name.size()))
            return failure<BinaryArchive>(diagnostics, "binary.archive.read", "archive expanded byte budget exceeded");
        for (const auto& g : object.groups) if (!account(g.name.size()))
            return failure<BinaryArchive>(diagnostics, "binary.archive.read", "archive expanded byte budget exceeded");
        limits.sections -= object.sections.size(); limits.symbols -= object.symbols.size();
        limits.relocations -= object.relocations.size(); limits.groups -= object.groups.size();
        archive.push_back({m.name, detail::from_stone(object)});
    }
    return archive;
}
std::optional<std::vector<std::uint8_t>> write_binary_archive(const BinaryArchive& archive, DiagnosticEngine& diagnostics) {
    try {
        if (archive.size() > detail::archive_limits().members) throw std::invalid_argument("archive member count exceeds 4096");
        detail::InputBudget budget;
        for (const auto& m : archive) { budget.account(m.name.size()); budget.add(m.object); }
        detail::stone::ObjectArchive input;
        for (const auto& m : archive) input.members.push_back({m.name, detail::to_stone(m.object)});
        auto result = detail::stone::emit_archive(input, detail::archive_limits());
        if (!result) return failure<std::vector<std::uint8_t>>(diagnostics, "binary.archive.write", result.error().message);
        return std::move(result.value());
    } catch (const std::invalid_argument& error) { return failure<std::vector<std::uint8_t>>(diagnostics, "binary.archive.write", error.what()); }
}
std::optional<BinaryImage> link_binary_objects(std::span<const BinaryObject> objects, std::span<const BinaryArchive> archives,
    const LinkContract& contract, const RawLinkOptions& options, DiagnosticEngine& diagnostics) {
    try {
        detail::InputBudget budget;
        for (const auto& object : objects) budget.add(object);
        for (const auto& archive : archives) {
            if (archive.size() > detail::archive_limits().members) throw std::invalid_argument("archive member count exceeds 4096");
            for (const auto& m : archive) { budget.account(m.name.size()); budget.add(m.object); }
        }
        std::vector<detail::stone::ObjectFile> roots;
        for (const auto& object : objects) roots.push_back(detail::to_stone(object));
        std::vector<detail::stone::ObjectArchive> libraries;
        for (const auto& archive : archives) {
            detail::stone::ObjectArchive library;
            for (const auto& m : archive) library.members.push_back({m.name, detail::to_stone(m.object)});
            libraries.push_back(std::move(library));
        }
        detail::stone::LinkOptions config; config.base_address = options.base_address; config.externals = options.externals;
        config.max_size = detail::binary_byte_limit; config.input_limit = detail::binary_byte_limit;
        auto result = detail::stone::link_archives(roots, libraries, detail::to_stone(contract), config);
        if (!result) return failure<BinaryImage>(diagnostics, "binary.link", result.error().message);
        BinaryImage image; image.data = std::move(result.value().bytes); image.symbols = std::move(result.value().symbols);
        image.discarded_groups = std::move(result.value().discarded_groups);
        for (const auto& s : result.value().sections) image.sections.push_back({s.object, s.section, s.name, s.address, s.offset, s.size, s.flags});
        return image;
    } catch (const std::invalid_argument& error) { return failure<BinaryImage>(diagnostics, "binary.link", error.what()); }
}
} // namespace mlvm
