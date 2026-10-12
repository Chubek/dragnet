#include <mlvm/binutils.hpp>
#include <algorithm>

namespace mlvm {
bool transform_binary_object(BinaryObject& object, const BinaryTransform& options, DiagnosticEngine& diagnostics) {
    if (!verify_binary_object(object, diagnostics)) return false;
    if (options.strip == StripPolicy::all && (!object.relocations.empty() || !object.groups.empty())) {
        diagnostics.error("binary.strip", "--strip-all requires an object without relocations or section groups"); return false;
    }
    BinaryObject result; result.format = object.format;
    std::vector<std::int32_t> sections(object.sections.size(), -1);
    for (std::size_t i = 0; i < object.sections.size(); ++i) {
        auto section = object.sections[i];
        const bool debug = section.name.starts_with(".debug") || section.name.starts_with(".zdebug") || section.name.starts_with(".stab");
        if (options.remove_sections.contains(section.name) ||
            (!options.only_sections.empty() && !options.only_sections.contains(section.name)) ||
            (options.strip == StripPolicy::debug && debug)) continue;
        if (auto it = options.rename_sections.find(section.name); it != options.rename_sections.end()) section.name = it->second;
        sections[i] = static_cast<std::int32_t>(result.sections.size()); result.sections.push_back(std::move(section));
    }
    std::set<std::size_t> referenced;
    for (const auto& r : object.relocations) if (sections[r.section] >= 0) referenced.insert(r.symbol);
    for (const auto& g : object.groups)
        if (std::any_of(g.members.begin(), g.members.end(), [&](const auto& m) { return sections[m.section] >= 0; })) referenced.insert(g.signature);
    std::vector<std::size_t> symbols(object.symbols.size(), SIZE_MAX);
    for (std::size_t i = 0; i < object.symbols.size(); ++i) {
        auto symbol = object.symbols[i];
        if (symbol.section >= 0 && sections[static_cast<std::size_t>(symbol.section)] < 0) {
            if (referenced.contains(i)) { diagnostics.error("binary.section", "retained relocation/group refers to a removed symbol section"); return false; }
            continue;
        }
        if (options.strip == StripPolicy::all || (options.strip == StripPolicy::locals && symbol.binding == Binding::local && !referenced.contains(i))) continue;
        if (symbol.section >= 0) symbol.section = sections[static_cast<std::size_t>(symbol.section)];
        symbols[i] = result.symbols.size(); result.symbols.push_back(std::move(symbol));
    }
    for (auto r : object.relocations) if (sections[r.section] >= 0) {
        r.section = static_cast<std::size_t>(sections[r.section]); r.symbol = symbols[r.symbol]; result.relocations.push_back(r);
    }
    for (auto g : object.groups) {
        std::vector<BinaryGroupMember> members;
        for (auto m : g.members) if (sections[m.section] >= 0) {
            m.section = static_cast<std::size_t>(sections[m.section]); members.push_back(m);
        }
        if (members.empty()) continue;
        g.members = std::move(members); g.signature = symbols[g.signature]; result.groups.push_back(std::move(g));
    }
    if (!verify_binary_object(result, diagnostics)) return false;
    object = std::move(result); return true;
}
bool scan_binary_strings(std::span<const std::uint8_t> bytes, const StringOptions& options,
    const std::function<void(std::uint64_t, std::string_view)>& emit, DiagnosticEngine& diagnostics) {
    if (!options.minimum || !emit || (options.encoding != StringEncoding::ascii && options.encoding != StringEncoding::utf16le_ascii && options.encoding != StringEncoding::utf16be_ascii)) {
        diagnostics.error("binary.strings", "invalid string length, encoding or callback"); return false;
    }
    std::string value; std::uint64_t start = 0;
    const std::size_t width = options.encoding == StringEncoding::ascii ? 1 : 2;
    auto flush = [&] { if (value.size() >= options.minimum) emit(start, value); value.clear(); };
    for (std::size_t p = 0; bytes.size() - p >= width; p += width) {
        const auto low = options.encoding == StringEncoding::utf16be_ascii ? p + 1 : p;
        const auto high = options.encoding == StringEncoding::utf16be_ascii ? p : p + 1;
        const auto byte = bytes[low];
        if (byte >= 32 && byte <= 126 && (width == 1 || bytes[high] == 0)) {
            if (value.empty()) start = p;
            value += static_cast<char>(byte);
        } else flush();
    }
    flush(); return true;
}
} // namespace mlvm
