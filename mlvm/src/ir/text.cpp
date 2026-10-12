#include <mlvm/ir.hpp>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mlvm {
namespace {
struct ParseFailure {};
class Parser {
public:
    Parser(Context& context, SourceManager& sources, Identifier file, DiagnosticEngine& diagnostics)
        : module(context), sources(sources), file(file), diagnostics(diagnostics), text(sources.file(file)->text) { next(); }
    Module run() {
        expect("mlvm"); expect("1"); expect("{");
        while (!quoted && (token == "kernel" || token == "apply")) {
            const bool definition = token == "kernel"; next();
            (definition ? module.kernel_sources : module.kernel_applications).push_back(string());
            expect(";");
        }
        region(module.body, 0); expect("}");
        if (quoted || !token.empty()) fail("end of file");
        module.rebuild_uses(); return std::move(module);
    }
private:
    Module module;
    SourceManager& sources;
    Identifier file;
    DiagnosticEngine& diagnostics;
    std::string_view text;
    std::size_t position = 0, start = 0;
    std::string token;
    bool quoted = false;
    std::map<std::string, ValueId> values;
    [[noreturn]] void fail(std::string expected) {
        diagnostics.error("ir.parse", "expected " + expected + ", found '" + token + "'", sources.location(file, start));
        throw ParseFailure{};
    }
    void next() {
        for (;;) {
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position;
            if (position + 1 < text.size() && text.substr(position, 2) == "//") {
                while (position < text.size() && text[position] != '\n') ++position;
            } else break;
        }
        start = position; token.clear(); quoted = false;
        if (position == text.size()) return;
        const char c = text[position++];
        if (c == '"') {
            quoted = true;
            while (position < text.size() && text[position] != '"') {
                char ch = text[position++];
                if (ch == '\\') {
                    if (position == text.size()) fail("escape sequence");
                    ch = text[position++];
                    if (ch == 'n') ch = '\n'; else if (ch == 'r') ch = '\r'; else if (ch == 't') ch = '\t';
                    else if (ch != '\\' && ch != '"') fail("valid string escape");
                }
                token += ch;
            }
            if (position == text.size()) fail("closing quote");
            ++position; return;
        }
        token += c;
        if (std::string_view("{}()[],:;=").find(c) != std::string_view::npos) return;
        while (position < text.size()) {
            const auto ch = static_cast<unsigned char>(text[position]);
            if (!(std::isalnum(ch) || ch == '_' || ch == '.' || ch == '-' || ch == '%')) break;
            token += text[position++];
        }
    }
    bool take(std::string_view expected) { if (quoted || token != expected) return false; next(); return true; }
    void expect(std::string_view expected) { if (!take(expected)) fail(std::string(expected)); }
    std::string string() { if (!quoted) fail("quoted string"); auto result = token; next(); return result; }
    std::string value_name() {
        if (quoted || token.size() < 2 || token[0] != '%') fail("SSA value (%name)");
        auto name = token; next(); return name;
    }
    void define(const std::string& name, ValueId id) {
        if (!values.emplace(name, id).second) fail("unique SSA name");
    }
    void region(Region& region, unsigned depth) {
        if (depth > 256) fail("region nesting at most 256");
        while (quoted || (token != "}" && !token.empty())) {
            expect("block"); expect("(");
            std::vector<std::string> names;
            std::vector<Identifier> types;
            if (!take(")")) {
                do { names.push_back(value_name()); expect(":"); types.push_back(module.context->intern(string())); } while (take(","));
                expect(")");
            }
            auto& block = module.add_block(region, types);
            for (std::size_t i = 0; i < names.size(); ++i) define(names[i], block.arguments[i]);
            expect("{");
            while (quoted || (token != "}" && !token.empty())) operation(block, depth);
            expect("}");
        }
    }
    void operation(Block& block, unsigned depth) {
        const auto location = sources.location(file, start);
        std::vector<std::string> names;
        std::vector<Identifier> types;
        if (!quoted && !token.empty() && token[0] == '%') {
            do { names.push_back(value_name()); expect(":"); types.push_back(module.context->intern(string())); } while (take(","));
            expect("=");
        }
        const auto name = string(); expect("(");
        std::vector<ValueId> operands;
        if (!take(")")) {
            do {
                const auto name = value_name(); const auto it = values.find(name);
                if (it == values.end()) fail("previously defined SSA value");
                operands.push_back(it->second);
            } while (take(","));
            expect(")");
        }
        auto& op = module.append(block, name, operands, types, location);
        for (std::size_t i = 0; i < names.size(); ++i) define(names[i], op.results[i]);
        expect("[");
        if (!take("]")) {
            do {
                auto key = string(); expect("="); auto value = string();
                if (!op.attributes.emplace(std::move(key), std::move(value)).second) fail("unique attribute key");
            } while (take(","));
            expect("]");
        }
        while (take("region")) {
            expect("{"); op.regions.emplace_back(); region(op.regions.back(), depth + 1); expect("}");
        }
        expect(";");
    }
};
std::string quote(std::string_view value) {
    std::string result = "\"";
    for (char c : value) {
        switch (c) {
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        default: result += c; break;
        }
    }
    return result + '"';
}
}
std::optional<Module> parse_ir(Context& context, SourceManager& sources, Identifier file, DiagnosticEngine& diagnostics) {
    if (!sources.file(file)) { diagnostics.error("ir.source", "invalid source file ID"); return std::nullopt; }
    try {
        auto module = Parser(context, sources, file, diagnostics).run();
        if (!verify(module, diagnostics)) return std::nullopt;
        return module;
    } catch (const ParseFailure&) { return std::nullopt; }
}
std::string print_ir(const Module& module) {
    std::ostringstream out;
    std::map<ValueId, std::size_t> names;
    auto value = [&](ValueId id, bool typed) {
        const auto [it, inserted] = names.emplace(id, names.size()); (void)inserted;
        out << '%' << it->second;
        if (typed) {
            const auto* v = module.value(id);
            out << ':' << quote(v ? module.context->spelling(v->type) : "<invalid>");
        }
    };
    std::function<void(const Region&, unsigned)> region = [&](const Region& r, unsigned depth) {
        const std::string indent(depth * 2, ' ');
        for (const auto& block : r.blocks) {
            out << indent << "block(";
            for (std::size_t i = 0; i < block.arguments.size(); ++i) { if (i) out << ", "; value(block.arguments[i], true); }
            out << ") {\n";
            for (const auto& op : block.operations) {
                out << indent << "  ";
                for (std::size_t i = 0; i < op.results.size(); ++i) { if (i) out << ", "; value(op.results[i], true); }
                if (!op.results.empty()) out << " = ";
                out << quote(op.name) << '(';
                for (std::size_t i = 0; i < op.operands.size(); ++i) { if (i) out << ", "; value(op.operands[i], false); }
                out << ") [";
                bool first = true;
                for (const auto& [key, val] : op.attributes) { if (!first) out << ", "; first = false; out << quote(key) << '=' << quote(val); }
                out << ']';
                for (const auto& child : op.regions) { out << " region {\n"; region(child, depth + 2); out << indent << "  }"; }
                out << ";\n";
            }
            out << indent << "}\n";
        }
    };
    out << "mlvm 1 {\n";
    for (const auto& source : module.kernel_sources) out << "  kernel " << quote(source) << ";\n";
    for (const auto& id : module.kernel_applications) out << "  apply " << quote(id) << ";\n";
    region(module.body, 1); out << "}\n"; return out.str();
}
} // namespace mlvm
