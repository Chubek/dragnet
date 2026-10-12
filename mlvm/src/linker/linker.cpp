#include <mlvm/object.hpp>
#include <algorithm>
#include <limits>

namespace mlvm {
std::optional<Object> link_relocatable(std::span<const Object> objects, DiagnosticEngine& diagnostics) {
    Object result;
    std::map<std::string, std::size_t> globals;
    for (const auto& object : objects) {
        if (!verify_object(object, diagnostics)) return std::nullopt;
        std::vector<std::pair<std::size_t, std::uint64_t>> sections;
        for (const auto& section : object.sections) {
            std::size_t index = result.sections.size();
            for (std::size_t i = 0; i < result.sections.size(); ++i) if (result.sections[i].name == section.name) index = i;
            if (index == result.sections.size()) result.sections.push_back({section.name, section.flags, section.alignment, {}});
            auto& target = result.sections[index];
            if (target.flags != section.flags) { diagnostics.error("link.section", "incompatible section flags for " + section.name); return std::nullopt; }
            const auto offset = (target.data.size() + section.alignment - 1) & ~(section.alignment - 1);
            target.alignment = std::max(target.alignment, section.alignment); target.data.resize(static_cast<std::size_t>(offset));
            target.data.insert(target.data.end(), section.data.begin(), section.data.end()); sections.emplace_back(index, offset);
        }
        std::vector<std::size_t> symbols;
        for (auto symbol : object.symbols) {
            if (symbol.section >= 0) {
                const auto [index, offset] = sections[static_cast<std::size_t>(symbol.section)];
                symbol.section = static_cast<std::int32_t>(index); symbol.value += offset;
            }
            if (symbol.binding == Binding::local) { symbols.push_back(result.symbols.size()); result.symbols.push_back(std::move(symbol)); continue; }
            const auto [it, inserted] = globals.emplace(symbol.name, result.symbols.size()); symbols.push_back(it->second);
            if (inserted) { result.symbols.push_back(std::move(symbol)); continue; }
            auto& existing = result.symbols[it->second];
            if (existing.section != -1 && symbol.section != -1 && existing.binding == Binding::global && symbol.binding == Binding::global) {
                diagnostics.error("link.duplicate", "duplicate strong symbol '" + symbol.name + "'"); return std::nullopt;
            }
            if (symbol.section != -1 && (existing.section == -1 || (existing.binding == Binding::weak && symbol.binding == Binding::global))) existing = std::move(symbol);
            else if (existing.section == -1 && symbol.binding == Binding::global) existing.binding = Binding::global;
        }
        for (auto relocation : object.relocations) {
            const auto [index, offset] = sections[relocation.section]; relocation.section = index; relocation.offset += offset;
            relocation.symbol = symbols[relocation.symbol]; result.relocations.push_back(relocation);
        }
    }
    if (!verify_object(result, diagnostics)) return std::nullopt;
    return result;
}
std::optional<std::vector<std::uint8_t>> link_executable(std::span<const Object> objects, std::string_view entry, DiagnosticEngine& diagnostics) {
    auto linked = link_relocatable(objects, diagnostics); if (!linked) return std::nullopt;
    auto& object = *linked;
    for (const auto& symbol : object.symbols) if (symbol.section == -1 && symbol.binding != Binding::weak) {
        diagnostics.error("link.undefined", "undefined symbol '" + symbol.name + "'"); return std::nullopt;
    }
    constexpr std::uint64_t base = 0x400000;
    std::vector<std::uint64_t> offsets(object.sections.size());
    std::size_t segments = 0;
    for (const auto& section : object.sections) if ((section.flags & 2) && !section.data.empty()) ++segments;
    if (segments > 64) { diagnostics.error("link.segments", "at most 64 loadable sections are supported"); return std::nullopt; }
    std::vector<std::uint8_t> bytes(64 + segments * 56);
    auto put = [&](std::size_t offset, std::uint64_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
    };
    std::size_t segment = 0;
    for (std::size_t i = 0; i < object.sections.size(); ++i) {
        const auto& section = object.sections[i];
        if (!(section.flags & 2)) { diagnostics.error("link.section", "non-allocated sections require relocatable linking"); return std::nullopt; }
        bytes.resize((bytes.size() + 4095) & ~std::size_t{4095}); offsets[i] = bytes.size();
        bytes.insert(bytes.end(), section.data.begin(), section.data.end());
        if (section.data.empty()) continue;
        const auto p = 64 + segment++ * 56;
        put(p, 1, 4); put(p + 4, 4 | ((section.flags & 1) ? 2 : 0) | ((section.flags & 4) ? 1 : 0), 4);
        put(p + 8, offsets[i], 8); put(p + 16, base + offsets[i], 8); put(p + 24, base + offsets[i], 8);
        put(p + 32, section.data.size(), 8); put(p + 40, section.data.size(), 8); put(p + 48, 4096, 8);
    }
    auto address = [&](const Symbol& symbol) -> std::uint64_t {
        return symbol.section == -1 ? 0 : symbol.section == -2 ? symbol.value : base + offsets[static_cast<std::size_t>(symbol.section)] + symbol.value;
    };
    const Symbol* start = nullptr;
    for (const auto& symbol : object.symbols) if (symbol.name == entry && symbol.section >= 0) start = &symbol;
    if (!start || !(object.sections[static_cast<std::size_t>(start->section)].flags & 4) || start->value >= object.sections[static_cast<std::size_t>(start->section)].data.size()) {
        diagnostics.error("link.entry", "entry must name a defined instruction in an executable section: " + std::string(entry)); return std::nullopt;
    }
    for (const auto& r : object.relocations) {
        const auto s = address(object.symbols[r.symbol]);
        const auto place = offsets[r.section] + r.offset;
        if (r.type != 1) {
            // Evaluate S - P + A as sign/magnitude, without wrapping a large
            // positive address into a small negative displacement.
            const auto p = base + place;
            bool negative = s < p;
            auto magnitude = negative ? p - s : s - p;
            const bool add_negative = r.addend < 0;
            const auto add_magnitude = add_negative ? std::uint64_t{0} - static_cast<std::uint64_t>(r.addend)
                                                     : static_cast<std::uint64_t>(r.addend);
            if (negative == add_negative) {
                if (magnitude > UINT64_MAX - add_magnitude) {
                    diagnostics.error("link.overflow", "PC-relative relocation arithmetic overflow"); return std::nullopt;
                }
                magnitude += add_magnitude;
            } else if (magnitude >= add_magnitude) magnitude -= add_magnitude;
            else { magnitude = add_magnitude - magnitude; negative = add_negative; }
            const auto limit = std::uint64_t{INT32_MAX} + (negative ? 1 : 0);
            if (magnitude > limit) {
                diagnostics.error("link.overflow", "PC-relative relocation exceeds signed 32-bit range"); return std::nullopt;
            }
            put(static_cast<std::size_t>(place), negative ? std::uint64_t{0} - magnitude : magnitude, 4);
            continue;
        }
        std::uint64_t value = s;
        if (r.addend >= 0) {
            if (s > UINT64_MAX - static_cast<std::uint64_t>(r.addend)) { diagnostics.error("link.overflow", "relocation addition overflow"); return std::nullopt; }
            value += static_cast<std::uint64_t>(r.addend);
        } else {
            const auto magnitude = std::uint64_t{0} - static_cast<std::uint64_t>(r.addend);
            if (value < magnitude && r.type == 1) { diagnostics.error("link.overflow", "absolute relocation underflow"); return std::nullopt; }
            value -= magnitude;
        }
        put(static_cast<std::size_t>(place), value, 8);
    }
    put(0, 0x464c457f, 4); bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    put(16, 2, 2); put(18, 62, 2); put(20, 1, 4); put(24, address(*start), 8); put(32, 64, 8);
    put(52, 64, 2); put(54, 56, 2); put(56, segments, 2); return bytes;
}
} // namespace mlvm
