#include <mlvm/object.hpp>
#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <stdexcept>

namespace mlvm {
namespace {
bool power2(std::uint64_t n) { return n && !(n & (n - 1)); }
struct Invalid : std::runtime_error { using std::runtime_error::runtime_error; };
struct Reader {
    std::span<const std::uint8_t> bytes;
    mutable std::size_t decoded_string_bytes = 0;
    void range(std::uint64_t offset, std::uint64_t size) const {
        if (offset > bytes.size() || size > bytes.size() - offset) throw Invalid("ELF range exceeds input size");
    }
    std::uint64_t get(std::uint64_t offset, unsigned size) const {
        range(offset, size); std::uint64_t result = 0;
        for (unsigned i = 0; i < size; ++i) result |= std::uint64_t(bytes[static_cast<std::size_t>(offset) + i]) << (i * 8);
        return result;
    }
    std::string string(std::uint64_t base, std::uint64_t size, std::uint64_t offset) const {
        range(base, size);
        if (offset >= size) throw Invalid("string offset exceeds string table");
        std::string result;
        for (auto i = offset; i < size; ++i) {
            auto c = bytes[static_cast<std::size_t>(base + i)]; if (!c) return result;
            // Repeated offsets into a long string must not amplify a small
            // corrupt input into quadratic owned-string allocation.
            if (decoded_string_bytes >= bytes.size()) throw Invalid("decoded ELF strings exceed input-size budget");
            ++decoded_string_bytes; result += static_cast<char>(c);
        }
        throw Invalid("unterminated ELF string");
    }
};
void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t string(std::vector<std::uint8_t>& table, std::string_view text) {
    if (table.size() + text.size() + 1 > UINT32_MAX) throw Invalid("string table too large");
    const auto index = static_cast<std::uint32_t>(table.size());
    table.insert(table.end(), text.begin(), text.end()); table.push_back(0); return index;
}
struct Header {
    std::uint32_t name = 0, type = 1;
    std::uint64_t flags = 0, offset = 0, size = 0;
    std::uint32_t link = 0, info = 0;
    std::uint64_t align = 1, entsize = 0;
    std::vector<std::uint8_t> data;
};
}
bool verify_object(const Object& object, DiagnosticEngine& diagnostics) {
    bool ok = true;
    auto fail = [&](std::string message) { diagnostics.error("object.invalid", std::move(message)); ok = false; };
    if (object.machine != 62) fail("only x86-64 objects are supported");
    std::set<std::string> names;
    for (const auto& section : object.sections) {
        if (section.name.empty() || section.name.find('\0') != std::string::npos || !names.insert(section.name).second) fail("invalid or duplicate section name");
        if (!power2(section.alignment) || section.alignment > 4096) fail("section alignment must be a power of two at most 4096");
        if (section.flags & ~std::uint64_t{7}) fail("unsupported section flags");
        if (section.name == ".symtab" || section.name == ".strtab" || section.name == ".shstrtab" || section.name.starts_with(".rela")) fail("reserved ELF metadata section name");
    }
    for (const auto& symbol : object.symbols) {
        if (symbol.name.empty() || symbol.name.find('\0') != std::string::npos) fail("invalid symbol name");
        if (static_cast<unsigned>(symbol.binding) > 2 || symbol.type > 2) fail("unsupported symbol binding or type");
        if (symbol.section < -2 || (symbol.section >= 0 && static_cast<std::size_t>(symbol.section) >= object.sections.size())) fail("symbol section out of range");
        else if (symbol.section >= 0) {
            const auto size = object.sections[static_cast<std::size_t>(symbol.section)].data.size();
            if (symbol.value > size || symbol.size > size - symbol.value) fail("symbol exceeds section bounds");
        }
        if (symbol.section == -1 && (symbol.value || symbol.size || symbol.binding == Binding::local)) fail("invalid undefined symbol");
    }
    for (const auto& relocation : object.relocations) {
        const auto width = relocation.type == 1 ? 8U : 4U;
        if (relocation.type != 1 && relocation.type != 2 && relocation.type != 4) fail("unsupported x86-64 relocation type");
        if (relocation.section >= object.sections.size() || relocation.symbol >= object.symbols.size()) fail("relocation section or symbol out of range");
        else {
            const auto size = object.sections[relocation.section].data.size();
            if (relocation.offset > size || width > size - relocation.offset) fail("relocation exceeds section bounds");
        }
    }
    return ok;
}
std::optional<Object> read_elf(std::span<const std::uint8_t> bytes, DiagnosticEngine& diagnostics) {
    try {
        Reader r{bytes}; r.range(0, 64);
        if (r.get(0, 4) != 0x464c457f || r.get(4, 1) != 2 || r.get(5, 1) != 1 || r.get(6, 1) != 1)
            throw Invalid("expected ELF64 little-endian version 1");
        if (r.get(16, 2) != 1 || r.get(18, 2) != 62 || r.get(20, 4) != 1 || r.get(48, 4) != 0)
            throw Invalid("only x86-64 ELF relocatable objects without processor flags are supported");
        if (r.get(52, 2) != 64 || r.get(58, 2) != 64 || r.get(56, 2) != 0) throw Invalid("unsupported ELF header layout");
        const auto base = r.get(40, 8), count = r.get(60, 2), names = r.get(62, 2);
        if (!count || names == 0 || names >= count) throw Invalid("extended or missing section indexes are unsupported");
        r.range(base, count * 64);
        std::vector<Header> headers(static_cast<std::size_t>(count));
        std::uint64_t payload = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto p = base + i * 64; auto& h = headers[i];
            h.name = static_cast<std::uint32_t>(r.get(p, 4)); h.type = static_cast<std::uint32_t>(r.get(p + 4, 4));
            h.flags = r.get(p + 8, 8); h.offset = r.get(p + 24, 8); h.size = r.get(p + 32, 8);
            h.link = static_cast<std::uint32_t>(r.get(p + 40, 4)); h.info = static_cast<std::uint32_t>(r.get(p + 44, 4));
            h.align = r.get(p + 48, 8); h.entsize = r.get(p + 56, 8);
            if (i && h.type != 8) r.range(h.offset, h.size);
            // Bound amplification from overlapping section ranges before any copies.
            if (i && h.type != 8) {
                if (h.size > bytes.size() - payload) throw Invalid("overlapping section payload exceeds input size");
                payload += h.size;
            }
            if (i && h.align && (!power2(h.align) || h.align > 4096)) throw Invalid("invalid or unsupported ELF section alignment");
            if (r.get(p + 16, 8)) throw Invalid("relocatable section has a nonzero address");
        }
        if (headers[0].type != 0) throw Invalid("invalid null section");
        const auto& shstr = headers[static_cast<std::size_t>(names)];
        if (shstr.type != 3) throw Invalid("section names are not a string table");
        Object result;
        std::map<std::size_t, std::size_t> section_map;
        std::size_t symtab = 0;
        for (std::size_t i = 1; i < count; ++i) {
            const auto& h = headers[i];
            const auto name = r.string(shstr.offset, shstr.size, h.name);
            if (h.type == 1) {
                section_map[i] = result.sections.size();
                result.sections.push_back({name, h.flags, h.align ? h.align : 1,
                    std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(h.offset), bytes.begin() + static_cast<std::ptrdiff_t>(h.offset + h.size))});
            } else if (h.type == 2) {
                if (symtab) throw Invalid("multiple symbol tables are unsupported");
                symtab = i;
            } else if (h.type != 3 && h.type != 4) throw Invalid("unsupported ELF section type " + std::to_string(h.type));
        }
        std::map<std::size_t, std::size_t> symbol_map;
        if (symtab) {
            const auto& h = headers[symtab];
            if (h.entsize != 24 || h.size % 24 || h.size < 24 || h.link >= count || headers[h.link].type != 3 || h.info > h.size / 24)
                throw Invalid("invalid ELF symbol table");
            for (unsigned j = 0; j < 24; ++j) if (r.get(h.offset + j, 1)) throw Invalid("invalid null symbol");
            const auto& str = headers[h.link];
            for (std::size_t i = 1; i < h.size / 24; ++i) {
                const auto p = h.offset + i * 24; const auto info = r.get(p + 4, 1), section = r.get(p + 6, 2);
                if (r.get(p + 5, 1)) throw Invalid("non-default symbol visibility is unsupported");
                if ((i < h.info) != ((info >> 4) == 0)) throw Invalid("inconsistent local symbol boundary");
                std::int32_t mapped = -1;
                if (section == 0xfff1) mapped = -2;
                else if (section) {
                    if (!section_map.contains(static_cast<std::size_t>(section))) throw Invalid("unsupported symbol section index");
                    mapped = static_cast<std::int32_t>(section_map.at(static_cast<std::size_t>(section)));
                }
                auto name = r.string(str.offset, str.size, r.get(p, 4));
                if ((info & 15) == 3 && name.empty() && mapped >= 0) name = result.sections[static_cast<std::size_t>(mapped)].name;
                symbol_map[i] = result.symbols.size();
                result.symbols.push_back({name, static_cast<Binding>(info >> 4), static_cast<std::uint8_t>((info & 15) == 3 ? 0 : info & 15), mapped, r.get(p + 8, 8), r.get(p + 16, 8)});
            }
        }
        for (std::size_t i = 1; i < count; ++i) if (headers[i].type == 4) {
            const auto& h = headers[i];
            if (!symtab || h.link != symtab || !section_map.contains(h.info) || h.entsize != 24 || h.size % 24) throw Invalid("invalid ELF relocation table");
            for (std::size_t j = 0; j < h.size / 24; ++j) {
                const auto p = h.offset + j * 24, info = r.get(p + 8, 8);
                if (!symbol_map.contains(static_cast<std::size_t>(info >> 32))) throw Invalid("invalid relocation symbol");
                result.relocations.push_back({section_map.at(h.info), symbol_map.at(static_cast<std::size_t>(info >> 32)), r.get(p, 8), static_cast<std::uint32_t>(info), std::bit_cast<std::int64_t>(r.get(p + 16, 8))});
            }
        }
        if (!verify_object(result, diagnostics)) return std::nullopt;
        return result;
    } catch (const Invalid& error) { diagnostics.error("elf.read", error.what()); return std::nullopt; }
}
std::optional<std::vector<std::uint8_t>> write_elf(const Object& object, DiagnosticEngine& diagnostics) {
    if (!verify_object(object, diagnostics)) return std::nullopt;
    try {
        std::vector<Header> headers(1); headers[0].type = 0; headers[0].align = 0;
        std::vector<std::uint8_t> names{0}, strings{0};
        for (const auto& section : object.sections) {
            Header h; h.name = string(names, section.name); h.flags = section.flags; h.align = section.alignment; h.data = section.data;
            headers.push_back(std::move(h));
        }
        const auto sym_index = static_cast<std::uint32_t>(headers.size());
        Header symbols; symbols.name = string(names, ".symtab"); symbols.type = 2; symbols.align = 8; symbols.entsize = 24; symbols.link = sym_index + 1;
        symbols.data.resize(24); symbols.info = 1;
        std::vector<std::size_t> symbol_map(object.symbols.size());
        for (bool local : {true, false}) for (std::size_t i = 0; i < object.symbols.size(); ++i) {
            const auto& symbol = object.symbols[i]; if ((symbol.binding == Binding::local) != local) continue;
            const auto offset = symbols.data.size(); symbol_map[i] = offset / 24; symbols.data.resize(offset + 24);
            put(symbols.data, offset, string(strings, symbol.name), 4);
            put(symbols.data, offset + 4, (static_cast<unsigned>(symbol.binding) << 4) | symbol.type, 1);
            put(symbols.data, offset + 6, symbol.section == -1 ? 0 : symbol.section == -2 ? 0xfff1 : static_cast<unsigned>(symbol.section) + 1, 2);
            put(symbols.data, offset + 8, symbol.value, 8); put(symbols.data, offset + 16, symbol.size, 8);
            if (local) ++symbols.info;
        }
        headers.push_back(std::move(symbols));
        Header str; str.name = string(names, ".strtab"); str.type = 3; str.data = std::move(strings); headers.push_back(std::move(str));
        for (std::size_t i = 0; i < object.sections.size(); ++i) {
            Header rel; rel.name = string(names, ".rela" + object.sections[i].name); rel.type = 4; rel.align = 8; rel.entsize = 24; rel.link = sym_index; rel.info = static_cast<std::uint32_t>(i + 1);
            for (const auto& r : object.relocations) if (r.section == i) {
                const auto p = rel.data.size(); rel.data.resize(p + 24);
                put(rel.data, p, r.offset, 8); put(rel.data, p + 8, (std::uint64_t(symbol_map[r.symbol]) << 32) | r.type, 8);
                put(rel.data, p + 16, static_cast<std::uint64_t>(r.addend), 8);
            }
            if (!rel.data.empty()) headers.push_back(std::move(rel));
        }
        const auto names_index = headers.size();
        Header shstr; shstr.name = string(names, ".shstrtab"); shstr.type = 3; shstr.data = std::move(names); headers.push_back(std::move(shstr));
        if (headers.size() >= 0xff00) throw Invalid("too many ELF sections");
        std::vector<std::uint8_t> bytes(64);
        for (std::size_t i = 1; i < headers.size(); ++i) {
            auto& h = headers[i]; bytes.resize((bytes.size() + h.align - 1) & ~(h.align - 1));
            h.offset = bytes.size(); h.size = h.data.size(); bytes.insert(bytes.end(), h.data.begin(), h.data.end());
        }
        bytes.resize((bytes.size() + 7) & ~std::size_t{7});
        const auto section_offset = bytes.size(); bytes.resize(section_offset + headers.size() * 64);
        for (std::size_t i = 0; i < headers.size(); ++i) {
            const auto p = section_offset + i * 64; const auto& h = headers[i];
            put(bytes, p, h.name, 4); put(bytes, p + 4, h.type, 4); put(bytes, p + 8, h.flags, 8);
            put(bytes, p + 24, h.offset, 8); put(bytes, p + 32, h.size, 8); put(bytes, p + 40, h.link, 4);
            put(bytes, p + 44, h.info, 4); put(bytes, p + 48, h.align, 8); put(bytes, p + 56, h.entsize, 8);
        }
        put(bytes, 0, 0x464c457f, 4); bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
        put(bytes, 16, 1, 2); put(bytes, 18, 62, 2); put(bytes, 20, 1, 4);
        put(bytes, 40, section_offset, 8); put(bytes, 52, 64, 2); put(bytes, 58, 64, 2);
        put(bytes, 60, headers.size(), 2); put(bytes, 62, names_index, 2); return bytes;
    } catch (const Invalid& error) { diagnostics.error("elf.write", error.what()); return std::nullopt; }
}
} // namespace mlvm
