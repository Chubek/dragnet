#pragma once
#include <dragscript/engine.hpp>
#include <array>
#include <map>
#include <optional>

namespace dragscript::detail {
using NodeId = std::size_t;

// A tag, not a magic string: the MLVM dialect derives its operation spelling from it.
enum class Kind {
    program, block, sequence, empty, expression, declaration, function,
    conditional, return_, literal, identifier, unary, binary, assign, call,
    property, object_literal, array_literal, member, assign_member,
    loop_while, loop_do, loop_for, loop_for_of, break_, continue_, update, compound
};

inline const char* kind_name(Kind kind) {
    constexpr const char* names[] = {"ds.program", "ds.block", "ds.sequence", "ds.empty", "ds.expression",
        "ds.declaration", "ds.function", "ds.if", "ds.return", "ds.literal", "ds.identifier",
        "ds.unary", "ds.binary", "ds.assign", "ds.call", "ds.property", "ds.object", "ds.array",
        "ds.member", "ds.assign_member", "ds.while", "ds.do", "ds.for", "ds.for_of", "ds.break",
        "ds.continue", "ds.update", "ds.compound"};
    return names[static_cast<unsigned>(kind)];
}

// Expressions evaluate to a runtime value; every other kind is a statement/completion.
inline bool is_expression_kind(Kind kind) {
    switch (kind) {
    case Kind::literal: case Kind::identifier: case Kind::unary: case Kind::binary:
    case Kind::assign: case Kind::call: case Kind::property: case Kind::object_literal:
    case Kind::array_literal: case Kind::member: case Kind::assign_member:
    case Kind::update: case Kind::compound:
        return true;
    default: return false;
    }
}
inline bool is_loop_kind(Kind kind) {
    return kind == Kind::loop_while || kind == Kind::loop_do || kind == Kind::loop_for || kind == Kind::loop_for_of;
}

struct Node {
    Kind kind = Kind::empty;
    SourceSpan span;
    std::vector<NodeId> children;
    std::string text; // Binding/loop-variable name, normalized operator, or access kind.
    Value literal;
    bool immutable = false; // const declaration, or const for-of loop binding.
    bool declared = false;  // for-of introduced its own binding (let/const form).
    bool prefix = false;    // Prefix form of ++/-- (this grammar omits the flag).
    std::array<bool, 3> parts{}; // for-statement: has initializer, test, update.
    std::string op;                      // Operator of ds.update / ds.compound (without "=").
    std::vector<std::string> parameters; // Function parameters.
    std::vector<std::string> bindings;   // Names declared by a for initializer.
};
struct Program {
    Source source;
    std::vector<Node> nodes;
    NodeId root = 0;
};
struct HostBinding { HostFunction callback; HostFunctionOptions options; };
using Hosts = std::map<std::string, HostBinding, std::less<>>;

Diagnostic diagnostic(const Source& source, std::string code, std::string message,
                      SourceSpan span = {}, Phase phase = Phase::compile, std::string limit = {});
bool identifier_name(std::string_view name);
std::string dump(const Program& program);
bool validate(const Program& program, std::vector<Diagnostic>& diagnostics);
std::optional<Program> frontend(Source source, const EngineOptions& limits, CompileOptions options,
                               std::vector<Diagnostic>& diagnostics);
// DSIR execution backend; see runtime/interpreter.cpp.
RunResult interpret(const Program& program, const Hosts& hosts, const EngineOptions& engine_options,
                    RunOptions options);
} // namespace dragscript::detail