#include <mlvm/object.hpp>
#include <mlvm/codegen.hpp>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>

namespace mlvm {
namespace {
std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}
bool number(std::string_view text, std::int64_t& value) {
    bool negative = !text.empty() && text.front() == '-';
    if (negative) text.remove_prefix(1);
    unsigned base = 10;
    if (text.starts_with("0x")) { base = 16; text.remove_prefix(2); }
    std::uint64_t magnitude = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), magnitude, static_cast<int>(base));
    if (text.empty() || r.ec != std::errc{} || r.ptr != text.data() + text.size() || magnitude > std::uint64_t{INT64_MAX} + (negative ? 1 : 0)) return false;
    value = negative ? (magnitude == std::uint64_t{INT64_MAX} + 1 ? INT64_MIN : -static_cast<std::int64_t>(magnitude)) : static_cast<std::int64_t>(magnitude);
    return true;
}
bool identifier(std::string_view name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_' || name[0] == '.')) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '.'; });
}
}
std::optional<Object> assemble(std::string_view text, SourceManager& sources, Identifier file, DiagnosticEngine& diagnostics) {
    Object object;
    object.sections.push_back({".text", 6, 16, {}});
    std::size_t section = 0, offset = 0;
    std::map<std::string, std::size_t> symbols;
    std::set<std::size_t> explicit_binding;
    bool ok = true;
    auto symbol = [&](const std::string& name) {
        auto [it, inserted] = symbols.emplace(name, object.symbols.size());
        if (inserted) object.symbols.push_back({name, Binding::global, 0, -1, 0, 0});
        return it->second;
    };
    auto emit = [&](std::uint64_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) object.sections[section].data.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    };
    auto instruction = [&](x86_64::Instruction inst) {
        auto bytes = x86_64::encode(inst, diagnostics);
        if (!bytes) { ok = false; return; }
        auto& data = object.sections[section].data; data.insert(data.end(), bytes->begin(), bytes->end());
    };
    while (offset < text.size()) {
        const auto end = text.find('\n', offset);
        auto line = trim(text.substr(offset, end == std::string_view::npos ? text.size() - offset : end - offset));
        const auto location = sources.location(file, offset);
        offset = end == std::string_view::npos ? text.size() : end + 1;
        if (const auto comment = line.find('#'); comment != std::string::npos) line = trim(line.substr(0, comment));
        auto fail = [&](std::string message) { diagnostics.error("asm.syntax", std::move(message), location); ok = false; };
        if (line.empty()) continue;
        if (const auto colon = line.find(':'); colon != std::string::npos) {
            const auto name = trim(line.substr(0, colon));
            if (!identifier(name)) { fail("invalid label"); continue; }
            const auto index = symbol(name); auto& s = object.symbols[index];
            if (s.section != -1) { fail("duplicate symbol '" + name + "'"); continue; }
            s.section = static_cast<std::int32_t>(section); s.value = object.sections[section].data.size();
            if (!explicit_binding.contains(index)) s.binding = Binding::local;
            line = trim(line.substr(colon + 1)); if (line.empty()) continue;
        }
        const auto split = line.find_first_of(" \t");
        const auto opcode = line.substr(0, split);
        const auto args = split == std::string::npos ? std::string{} : trim(line.substr(split + 1));
        if (opcode == ".text" || opcode == ".data" || opcode == ".rodata" || opcode == ".section") {
            const auto name = opcode == ".section" ? args : opcode;
            if ((opcode != ".section" && !args.empty()) || (name != ".text" && name != ".data" && name != ".rodata")) { fail("supported sections: .text, .data, .rodata"); continue; }
            section = object.sections.size();
            for (std::size_t i = 0; i < object.sections.size(); ++i) if (object.sections[i].name == name) section = i;
            if (section == object.sections.size()) object.sections.push_back({name, name == ".text" ? 6U : name == ".data" ? 3U : 2U, 1, {}});
        } else if (opcode == ".globl" || opcode == ".global" || opcode == ".weak") {
            if (!identifier(args)) { fail("expected one symbol name"); continue; }
            const auto i = symbol(args); explicit_binding.insert(i); object.symbols[i].binding = opcode == ".weak" ? Binding::weak : Binding::global;
        } else if (opcode == ".type") {
            const auto comma = args.find(',');
            const auto name = trim(args.substr(0, comma));
            const auto type = comma == std::string::npos ? "" : trim(args.substr(comma + 1));
            if (!identifier(name) || (type != "@function" && type != "@object")) { fail("expected .type symbol, @function or @object"); continue; }
            object.symbols[symbol(name)].type = type == "@function" ? 2 : 1;
        } else if (opcode == ".size") {
            const auto comma = args.find(','); const auto name = trim(args.substr(0, comma));
            const auto expression = comma == std::string::npos ? "" : trim(args.substr(comma + 1));
            std::int64_t size = 0;
            if (!identifier(name)) { fail("expected symbol name"); continue; }
            auto& s = object.symbols[symbol(name)];
            if (expression == ".-" + name && s.section == static_cast<std::int32_t>(section)) s.size = object.sections[section].data.size() - s.value;
            else if (number(expression, size) && size >= 0) s.size = static_cast<std::uint64_t>(size);
            else fail("expected nonnegative size or .-symbol in the same section");
        } else if (opcode == ".balign") {
            std::int64_t alignment = 0;
            if (!number(args, alignment) || alignment <= 0 || alignment > 4096 || (alignment & (alignment - 1))) { fail("alignment must be a power of two at most 4096"); continue; }
            auto& s = object.sections[section]; s.alignment = std::max(s.alignment, static_cast<std::uint64_t>(alignment));
            s.data.resize((s.data.size() + static_cast<std::size_t>(alignment) - 1) & ~(static_cast<std::size_t>(alignment) - 1));
        } else if (opcode == ".byte" || opcode == ".long" || opcode == ".quad") {
            const unsigned width = opcode == ".byte" ? 1 : opcode == ".long" ? 4 : 8;
            std::int64_t value = 0;
            if (number(args, value)) {
                if ((width == 1 && (value < -128 || value > 255)) || (width == 4 && (value < INT32_MIN || value > UINT32_MAX))) fail("data value does not fit directive width");
                else emit(static_cast<std::uint64_t>(value), width);
            } else if (width == 8 && identifier(args)) {
                object.relocations.push_back({section, symbol(args), object.sections[section].data.size(), 1, 0}); emit(0, 8);
            } else fail("expected integer, or a symbol for .quad");
        } else if ((opcode == "ret" || opcode == "nop" || opcode == "syscall") && args.empty()) {
            instruction({opcode == "ret" ? x86_64::Opcode::ret : opcode == "nop" ? x86_64::Opcode::nop : x86_64::Opcode::syscall});
        } else if ((opcode == "call" || opcode == "jmp") && identifier(args)) {
            object.relocations.push_back({section, symbol(args), object.sections[section].data.size() + 1, 2, -4});
            instruction({opcode == "call" ? x86_64::Opcode::call_rel32 : x86_64::Opcode::jump_rel32});
        } else if (opcode == "mov" || opcode == "movl") {
            const auto comma = args.find(',');
            const auto immediate = trim(args.substr(0, comma)); const auto reg = comma == std::string::npos ? "" : trim(args.substr(comma + 1));
            std::int64_t value = 0;
            const std::map<std::string, unsigned> regs{{"%eax", 0}, {"%ecx", 1}, {"%edx", 2}, {"%ebx", 3}, {"%esp", 4}, {"%ebp", 5}, {"%esi", 6}, {"%edi", 7}};
            if (regs.contains(immediate) && regs.contains(reg)) {
                instruction({x86_64::Opcode::mov_reg32, static_cast<std::uint8_t>(regs.at(immediate)), static_cast<std::uint8_t>(regs.at(reg)), 0}); continue;
            }
            if (immediate.empty() || immediate[0] != '$' || !number(std::string_view(immediate).substr(1), value) || value < INT32_MIN || value > UINT32_MAX || !regs.contains(reg)) { fail("supported mov syntax: mov $imm32, %r32 or mov %r32, %r32"); continue; }
            instruction({x86_64::Opcode::mov_imm32, 0, static_cast<std::uint8_t>(regs.at(reg)), static_cast<std::uint32_t>(value)});
        } else if (opcode == "add" || opcode == "addl" || opcode == "sub" || opcode == "subl") {
            const auto comma = args.find(',');
            const auto immediate = trim(args.substr(0, comma)); const auto reg = comma == std::string::npos ? "" : trim(args.substr(comma + 1));
            std::int64_t value = 0;
            const std::map<std::string, unsigned> regs{{"%eax", 0}, {"%ecx", 1}, {"%edx", 2}, {"%ebx", 3}, {"%esp", 4}, {"%ebp", 5}, {"%esi", 6}, {"%edi", 7}};
            const bool is_add = opcode[0] == 'a';
            if (immediate.empty() || immediate[0] != '$' || !number(std::string_view(immediate).substr(1), value) || value < INT32_MIN || value > UINT32_MAX || !regs.contains(reg)) {
                fail(is_add ? "supported add syntax: add $imm32, %r32" : "supported sub syntax: sub $imm32, %r32"); continue;
            }
            instruction({is_add ? x86_64::Opcode::add_imm32 : x86_64::Opcode::sub_imm32, 0, static_cast<std::uint8_t>(regs.at(reg)), static_cast<std::uint32_t>(value)});
        } else if (opcode == "push" || opcode == "pushl" || opcode == "pop" || opcode == "popl") {
            const std::map<std::string, unsigned> regs{{"%eax", 0}, {"%ecx", 1}, {"%edx", 2}, {"%ebx", 3}, {"%esp", 4}, {"%ebp", 5}, {"%esi", 6}, {"%edi", 7}};
            if (!regs.contains(args)) { fail(opcode[1] == 'u' ? "supported push syntax: push %r32" : "supported pop syntax: pop %r32"); continue; }
            if (opcode[1] == 'u') instruction({x86_64::Opcode::push_reg32, static_cast<std::uint8_t>(regs.at(args)), 0, 0});
            else instruction({x86_64::Opcode::pop_reg32, 0, static_cast<std::uint8_t>(regs.at(args)), 0});
        } else fail("unsupported instruction or directive '" + opcode + "'");
    }
    if (!ok || !verify_object(object, diagnostics)) return std::nullopt;
    return object;
}
} // namespace mlvm
