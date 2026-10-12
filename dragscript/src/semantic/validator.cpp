#include "dsir.hpp"
#include <set>

namespace dragscript::detail {
namespace {
// A declaration may only appear where it gets a scope of its own.
bool declares_binding(const Node& node) {
    if (node.kind == Kind::sequence) return true;
    return node.kind == Kind::function;
}
} // namespace
bool validate(const Program& program, std::vector<Diagnostic>& diagnostics) {
    auto fail = [&](const Node& node, std::string code, std::string message) {
        diagnostics.push_back(diagnostic(program.source, std::move(code), std::move(message), node.span));
        return false;
    };
    std::function<bool(NodeId, bool, unsigned, const std::vector<std::string>&)> visit;
    visit = [&](NodeId id, bool in_function, unsigned loops, const std::vector<std::string>& parameters) {
        const auto& node = program.nodes[id];
        if (node.kind == Kind::return_ && !in_function) return fail(node, "DS_RETURN", "return is only allowed inside a function");
        if ((node.kind == Kind::break_ || node.kind == Kind::continue_) && loops == 0)
            return fail(node, "DS_SCOPE", std::string(node.kind == Kind::break_ ? "break" : "continue") + " requires a loop");
        if (node.kind == Kind::update || node.kind == Kind::compound) {
            if (node.children.empty() || program.nodes[node.children[0]].kind != Kind::identifier)
                return fail(node, "DS_SEMANTIC", "Update requires an identifier target");
        }
        if (node.kind == Kind::loop_for && !node.bindings.empty()) {
            std::set<std::string> names;
            for (const auto& binding : node.bindings)
                if (!names.insert(binding).second) return fail(node, "DS_DUPLICATE", "Duplicate loop binding: " + binding);
        }
        auto require_block_body = [&](std::size_t index) {
            const auto& body = program.nodes[node.children[index]];
            if (declares_binding(body)) return fail(body, "DS_SCOPE", "Declarations in this branch require a block");
            return true;
        };
        if (node.kind == Kind::conditional) {
            if (!require_block_body(1)) return false;
            if (node.children.size() == 3 && !require_block_body(2)) return false;
        } else if (node.kind == Kind::loop_do) { if (!require_block_body(0)) return false; }
        else if (is_loop_kind(node.kind)) { if (!require_block_body(node.children.size() - 1)) return false; }
        if (node.kind == Kind::program || node.kind == Kind::block) {
            std::set<std::string> names;
            for (const auto& p : parameters)
                if (!names.insert(p).second) return fail(node, "DS_DUPLICATE", "Duplicate parameter: " + p);
            auto declaration = [&](const Node& decl) {
                if (!names.insert(decl.text).second) return fail(decl, "DS_DUPLICATE", "Duplicate binding: " + decl.text);
                return true;
            };
            for (auto child : node.children) {
                const auto& n = program.nodes[child];
                if (n.kind == Kind::function && !declaration(n)) return false;
                if (n.kind == Kind::sequence)
                    for (auto d : n.children) if (!declaration(program.nodes[d])) return false;
            }
        }
        if (node.kind == Kind::function) return visit(node.children[0], true, loops, node.parameters);
        const unsigned inner = loops + (is_loop_kind(node.kind) ? 1 : 0);
        for (auto child : node.children)
            if (!visit(child, in_function, inner, {})) return false;
        return true;
    };
    return visit(program.root, false, 0, {});
}
} // namespace dragscript::detail