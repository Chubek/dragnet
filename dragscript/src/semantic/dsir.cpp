#include "dsir.hpp"
#include <sstream>

namespace dragscript::detail {
Diagnostic diagnostic(const Source& source, std::string code, std::string message,
                      SourceSpan span, Phase phase, std::string limit) {
    return {std::move(code), std::move(message), source.name, source.origin, span, phase, std::move(limit), {}};
}
bool identifier_name(std::string_view name) {
    constexpr std::string_view reserved = " break case catch class const continue debugger default delete do else enum export extends false finally for function if import in instanceof new null return super switch this throw true try typeof var void while with yield let static implements interface package private protected public ";
    if (reserved.find(" " + std::string(name) + " ") != std::string_view::npos) return false;
    auto first = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; };
    if (name.empty() || !first(name.front())) return false;
    for (char c : name) if (!first(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}
std::string dump(const Program& program) {
    constexpr const char* short_names[] = {"program", "block", "sequence", "empty", "expression", "declaration",
        "function", "if", "return", "literal", "identifier", "unary", "binary", "assign", "call", "property",
        "object", "array", "member", "assign_member", "while", "do", "for", "for_of", "break", "continue",
        "update", "compound"};
    std::ostringstream out;
    for (NodeId id = 0; id < program.nodes.size(); ++id) {
        const auto& n = program.nodes[id];
        out << id << ' ' << short_names[static_cast<unsigned>(n.kind)] << " [" << n.span.begin.byte << ','
            << n.span.end.byte << ')';
        if (!n.text.empty()) out << ' ' << n.text;
        if (n.kind == Kind::literal) out << ' ' << n.literal.display();
        if (n.kind == Kind::declaration) out << (n.immutable ? " const" : " let");
        if (n.kind == Kind::update && n.prefix) out << " prefix";
        for (const auto& parameter : n.parameters) out << " parameter=" << parameter;
        for (const auto& binding : n.bindings) out << " binding=" << binding;
        for (auto child : n.children) out << " @" << child;
        out << '\n';
    }
    return out.str();
}
} // namespace dragscript::detail