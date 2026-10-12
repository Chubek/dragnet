#include "cli.hpp"
#include <mlvm/codegen.hpp>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace mlvm::tools {
namespace {
std::string section_name(const BinaryObject& object, std::int32_t section) {
    return section == -1 ? "UND" : section == -2 ? "ABS" : object.sections[static_cast<std::size_t>(section)].name;
}
bool selected(const BinarySection& section, const std::set<std::string>& filter) { return filter.empty() || filter.contains(section.name); }
}
void print_header(const BinaryObject& object) {
    const auto& f = object.format;
    std::cout << "ELF" << f.bits << (f.endianness == Endianness::little ? " little-endian " : " big-endian ");
    if (f.machine == 62) std::cout << "x86-64";
    else if (f.machine == 3) std::cout << "i386";
    else std::cout << "machine=" << f.machine;
    std::cout << " relocatable\nflags=" << f.flags << " osabi=" << unsigned(f.osabi) << " abi_version=" << unsigned(f.abi_version) << '\n';
}
void print_sections(const BinaryObject& object, const std::set<std::string>& filter) {
    for (std::size_t i = 0; i < object.sections.size(); ++i) {
        const auto& s = object.sections[i]; if (!selected(s, filter)) continue;
        std::cout << "section " << i << ' ' << s.name << " size=" << s.size() << " align=" << s.alignment
            << " flags=" << s.flags << " type=" << s.type << " entry_size=" << s.entry_size << '\n';
    }
}
void print_symbols(const BinaryObject& object, std::span<const std::size_t> indexes, bool metadata) {
    for (auto index : indexes) {
        const auto& s = object.symbols[index]; const char binding = s.binding == Binding::local ? 'l' : s.binding == Binding::weak ? 'w' : 'g';
        std::cout << std::hex << std::setw(object.format.bits / 4) << std::setfill('0') << s.value << std::dec << std::setfill(' ')
            << ' ' << binding << ' ' << section_name(object, s.section) << ' ' << s.name;
        if (metadata) std::cout << " size=" << s.size << " type=" << unsigned(s.type) << " visibility=" << unsigned(s.visibility);
        std::cout << '\n';
    }
}
void print_symbols(const BinaryObject& object) {
    std::vector<std::size_t> indexes(object.symbols.size()); std::iota(indexes.begin(), indexes.end(), 0); print_symbols(object, indexes, true);
}
void print_relocations(const BinaryObject& object, const std::set<std::string>& filter) {
    for (const auto& r : object.relocations) {
        const auto& s = object.sections[r.section]; if (!selected(s, filter)) continue;
        std::cout << "reloc " << s.name << '+' << r.offset << " type=" << r.type << ' ' << object.symbols[r.symbol].name;
        if (r.implicit_addend) std::cout << " addend=implicit (REL)\n";
        else std::cout << " addend=" << r.addend << " (RELA)\n";
    }
}
void print_groups(const BinaryObject& object) {
    for (const auto& g : object.groups) {
        std::cout << "group " << g.name << " signature=" << object.symbols[g.signature].name << " flags=" << g.flags;
        for (const auto& m : g.members) std::cout << ' ' << (m.kind == BinaryGroupMember::Kind::content ? "" : m.kind == BinaryGroupMember::Kind::rel ? ".rel" : ".rela") << object.sections[m.section].name;
        std::cout << '\n';
    }
}
void print_contents(const BinaryObject& object, bool disassemble, const std::set<std::string>& filter) {
    if (disassemble && (object.format.bits != 64 || object.format.machine != 62 || object.format.endianness != Endianness::little))
        throw Error(1, "disassembly supports only the documented x86-64 little-endian instruction subset");
    for (const auto& s : object.sections) {
        if (!selected(s, filter) || (disassemble && !(s.flags & 4))) continue;
        std::cout << (disassemble ? "Disassembly of " : "Contents of ") << s.name << ":\n";
        if (s.type == 8) { std::cout << "  NOBITS size=" << s.size() << '\n'; continue; }
        for (std::size_t p = 0; p < s.data.size();) {
            std::cout << "  " << std::hex << std::setw(8) << std::setfill('0') << p << ": ";
            if (disassemble) {
                if (auto decoded = x86_64::decode(std::span(s.data).subspan(p))) {
                    std::cout << x86_64::format(decoded->instruction); p += decoded->size;
                } else { std::cout << ".byte 0x" << std::setw(2) << unsigned(s.data[p]); ++p; }
            } else {
                const auto end = std::min(p + 16, s.data.size());
                for (; p < end; ++p) std::cout << std::setw(2) << unsigned(s.data[p]) << ' ';
            }
            std::cout << std::dec << std::setfill(' ') << '\n';
        }
    }
}
} // namespace mlvm::tools
