#include "session.hpp"
#include <limits>

namespace dragscript::detail {
namespace {
// Execution backend over DSIR source nodes. Semantics live in Session so the
// MLVM tree backend can share exactly these value, scope, host and budget contracts.
class Interpreter {
public:
    Interpreter(const Program& program, const Hosts& hosts, const EngineOptions& limits, RunOptions options)
        : program(program), session(program.source, hosts, limits, options) {}
    RunResult run();
private:
    const Program& program;
    Session session;
    const Node& node(NodeId id) const { return program.nodes[id]; }
    Binding& reference(SourceSpan span, EnvId env, const std::string& name) {
        auto* binding = session.lookup(env, name);
        if (!binding) session.fail(span, "DS_REFERENCE", "Unknown identifier: " + name);
        if (!binding->initialized) session.fail(span, "DS_TDZ", "Binding accessed before initialization: " + name);
        return *binding;
    }
    void prepare(const Node& block, EnvId env);
    Completion scope(NodeId id, EnvId env);
    Completion execute(NodeId id, EnvId env);
    RuntimeValue evaluate(NodeId id, EnvId env);
    RuntimeValue call(SourceSpan span, const RuntimeValue& target, const std::vector<RuntimeValue>& args);
    // A statement child yields its value through the completion record.
    bool truthy_completion(const Completion& completion) {
        return completion.has_value && session.truthy(completion.value);
    }
};
void Interpreter::prepare(const Node& block, EnvId env) {
    for (auto child : block.children) {
        const auto& n = node(child);
        if (n.kind == Kind::sequence) prepare(n, env); // Nested sequences declare immediately.
        else if (n.kind == Kind::declaration) {
            Session::Depth depth(session, n.span);
            session.declare(n.span, env, n.text, n.immutable);
        } else if (n.kind == Kind::function) {
            Session::Depth depth(session, n.span);
            Callable callable;
            callable.closure = env; callable.name = n.text; callable.parameters = n.parameters; callable.node = child;
            const auto id = session.add_callable(std::move(callable));
            auto& binding = session.declare(n.span, env, n.text, false);
            binding.value = ScriptFunction{id}; binding.initialized = true;
        }
    }
}
Completion Interpreter::scope(NodeId id, EnvId env) {
    const auto& block = node(id);
    prepare(block, env);
    Completion result;
    for (auto child : block.children) {
        auto next = execute(child, env);
        if (next.kind != CompletionKind::normal) {
            // break and continue carry an empty value, so a block's accumulated
            // completion value survives them; return propagates its own value.
            if (next.kind != CompletionKind::returned && !next.has_value && result.has_value) {
                next.value = result.value;
                next.has_value = true;
            }
            return next;
        }
        if (next.has_value) result = std::move(next);
    }
    return result;
}
Completion Interpreter::execute(NodeId id, EnvId env) {
    const auto& n = node(id);
    Session::Depth depth(session, n.span);
    switch (n.kind) {
    case Kind::program: return scope(id, env);
    case Kind::block: return scope(id, session.make_env(n.span, env));
    case Kind::sequence:
        for (auto child : n.children) execute(child, env);
        return {};
    case Kind::empty: case Kind::function: return {};
    case Kind::expression: return normal_completion(evaluate(n.children[0], env));
    case Kind::declaration: {
        auto* target = session.lookup(env, n.text);
        if (!target) session.fail(n.span, "DS_INTERNAL", "Declaration was not prepared");
        auto& binding = *target;
        binding.value = n.children.empty() ? RuntimeValue(Value{}) : evaluate(n.children[0], env);
        binding.initialized = true;
        return {};
    }
    case Kind::conditional: {
        if (session.truthy(evaluate(n.children[0], env))) {
            auto result = execute(n.children[1], env);
            result.has_value = true; // UpdateEmpty(undefined) keeps the script's completion chain.
            return result;
        }
        if (n.children.size() == 3) {
            auto result = execute(n.children[2], env);
            result.has_value = true;
            return result;
        }
        return normal_completion(Value{});
    }
    case Kind::return_: return {CompletionKind::returned, n.children.empty() ? RuntimeValue(Value{}) : evaluate(n.children[0], env), true};
    case Kind::loop_while: case Kind::loop_do: {
        const bool is_while = n.kind == Kind::loop_while;
        const auto test = n.children[is_while ? 0 : 1], body = n.children[is_while ? 1 : 0];
        Completion result;
        auto step = [&] {
            auto iteration = execute(body, env);
            if (iteration.kind == CompletionKind::returned) return iteration;
            // A loop's completion is the completion of its last iteration.
            result = absorbed(iteration);
            return iteration;
        };
        if (!is_while && step().kind == CompletionKind::returned) return result; // do-while runs its body first.
        // while evaluates its test before every iteration, including the first.
        while (session.truthy(evaluate(test, env))) {
            const auto iteration = step();
            if (iteration.kind == CompletionKind::returned) return iteration;
            if (iteration.kind == CompletionKind::broke) break;
        }
        return result;
    }
    case Kind::loop_for: {
        const auto env_for = session.make_env(n.span, env);
        std::size_t cursor = 0;
        if (n.parts[0]) {
            if (!n.bindings.empty()) prepare(node(n.children[cursor]), env_for);
            execute(n.children[cursor], env_for);
            ++cursor;
        }
        const auto test = cursor;
        if (n.parts[1]) ++cursor;
        const auto update = cursor;
        if (n.parts[2]) ++cursor;
        const auto body = cursor;
        EnvId current = env_for;
        Completion result;
        while (!n.parts[1] || truthy_completion(execute(n.children[test], current))) {
            EnvId iteration_env = current;
            if (!n.bindings.empty()) { // Per-iteration bindings copy the previous iteration's values.
                iteration_env = session.make_env(n.span, env_for);
                for (const auto& binding : n.bindings) {
                    auto* source = session.lookup(current, binding);
                    auto& target = session.declare(n.span, iteration_env, binding, false);
                    target.value = source ? source->value : RuntimeValue(Value{});
                    target.initialized = true;
                }
            }
            auto iteration = execute(n.children[body], iteration_env);
            if (iteration.kind == CompletionKind::returned) return iteration;
            if (iteration.kind == CompletionKind::broke) { result = absorbed(iteration); break; }
            result = absorbed(iteration); // A loop's completion is its last iteration's completion.
            if (n.parts[2]) execute(n.children[update], iteration_env);
            current = iteration_env;
        }
        return result;
    }
    case Kind::loop_for_of: {
        const auto iterable = evaluate(n.children[0], env);
        std::vector<RuntimeValue> items;
        if (const auto* array = std::get_if<ArrayId>(&iterable)) items = session.arrays[array->index].elements;
        else if (const auto* p = std::get_if<Value>(&iterable)) {
            if (p->tag() != Value::Tag::string) session.fail(n.span, "DS_TYPE", "for-of requires an array or string");
            const auto& text = p->string(); // Iteration is by code point, not code unit.
            for (std::size_t i = 0; i < text.size();) {
                std::size_t length = 1;
                if (text[i] >= 0xd800 && text[i] < 0xdc00 && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] < 0xe000)
                    length = 2;
                items.push_back(Value(text.substr(i, length))); i += length;
            }
        } else session.fail(n.span, "DS_TYPE", "for-of requires an array or string");
        Completion result;
        for (auto& item : items) {
            EnvId iteration_env = env;
            if (n.declared) {
                iteration_env = session.make_env(n.span, env);
                auto& binding = session.declare(n.span, iteration_env, n.text, n.immutable);
                binding.value = std::move(item); binding.initialized = true;
            } else {
                auto& binding = reference(n.span, env, n.text);
                if (binding.immutable) session.fail(n.span, "DS_CONST", "Assignment to immutable binding: " + n.text);
                binding.value = std::move(item);
            }
            auto iteration = execute(n.children[1], iteration_env);
            if (iteration.kind == CompletionKind::returned) return iteration;
            if (iteration.kind == CompletionKind::broke) { result = absorbed(iteration); break; }
            result = absorbed(iteration);
        }
        return result;
    }
    case Kind::break_: return abrupt_completion(CompletionKind::broke);
    case Kind::continue_: return abrupt_completion(CompletionKind::continued);
    default: session.fail(n.span, "DS_INTERNAL", "Expression used as a statement");
    }
}
RuntimeValue Interpreter::evaluate(NodeId id, EnvId env) {
    const auto& n = node(id);
    Session::Depth depth(session, n.span);
    switch (n.kind) {
    case Kind::literal: session.check_string(n.span, n.literal); return n.literal;
    case Kind::identifier: return reference(n.span, env, n.text).value;
    case Kind::assign: {
        auto value = evaluate(n.children[0], env);
        auto& binding = reference(n.span, env, n.text);
        if (binding.immutable) session.fail(n.span, "DS_CONST", "Assignment to immutable binding: " + n.text);
        binding.value = value;
        return value;
    }
    case Kind::assign_member: {
        auto base = evaluate(n.children[0], env);
        auto key = session.to_key(n.span, evaluate(n.children[1], env));
        auto value = evaluate(n.children[2], env);
        session.set_property(n.span, base, key, value);
        return value;
    }
    case Kind::member: {
        auto base = evaluate(n.children[0], env);
        auto key = session.to_key(n.span, evaluate(n.children[1], env));
        return session.get_property(n.span, base, key);
    }
    case Kind::object_literal: {
        const auto object = session.new_object(n.span);
        for (auto child : n.children) { // Keys and values evaluate left-to-right in source order.
            const auto& entry = node(child);
            auto key = session.to_key(n.span, evaluate(entry.children[0], env));
            session.set_own_property(n.span, object, key, evaluate(entry.children[1], env));
        }
        return ObjectId{object};
    }
    case Kind::array_literal: {
        const auto array = session.new_array(n.span);
        for (auto child : n.children) session.array_push(n.span, array, evaluate(child, env));
        return ArrayId{array};
    }
    case Kind::update: {
        auto& binding = reference(n.span, env, n.text);
        if (binding.immutable) session.fail(n.span, "DS_CONST", "Assignment to immutable binding: " + n.text);
        const auto previous = session.to_number(n.span, binding.value);
        const auto next = n.op == "++" ? previous + 1 : previous - 1;
        binding.value = Value(next);
        return Value(n.prefix ? next : previous);
    }
    case Kind::compound: {
        auto& current = reference(n.span, env, n.text);
        auto right = evaluate(n.children[1], env);
        auto value = session.binary(n.span, n.op, current.value, right);
        if (current.immutable) session.fail(n.span, "DS_CONST", "Assignment to immutable binding: " + n.text);
        current.value = value;
        return value;
    }
    case Kind::unary: {
        if (n.text == "typeof") {
            const auto& operand = node(n.children[0]);
            if (operand.kind == Kind::identifier && !session.lookup(env, operand.text))
                return Value::text(u"undefined"); // Unresolvable typeof is not a ReferenceError.
            auto target = evaluate(n.children[0], env);
            if (const auto* p = std::get_if<Value>(&target)) {
                switch (p->tag()) {
                case Value::Tag::undefined: return Value::text(u"undefined");
                case Value::Tag::null: return Value::text(u"object");
                case Value::Tag::boolean: return Value::text(u"boolean");
                case Value::Tag::number: return Value::text(u"number");
                case Value::Tag::string: return Value::text(u"string");
                }
            }
            if (std::holds_alternative<ScriptFunction>(target) || std::holds_alternative<HostReference>(target))
                return Value::text(u"function");
            return Value::text(u"object");
        }
        if (n.text == "void") { evaluate(n.children[0], env); return Value{}; }
        auto value = evaluate(n.children[0], env);
        if (n.text == "!") return Value(!session.truthy(value));
        const auto number = session.to_number(n.span, value);
        return Value(n.text == "-" ? -number : number);
    }
    case Kind::binary: {
        auto left = evaluate(n.children[0], env);
        if (n.text == "&&" && !session.truthy(left)) return left;
        if (n.text == "||" && session.truthy(left)) return left;
        if (n.text == "??" && !is_nullish(left)) return left;
        auto right = evaluate(n.children[1], env);
        if (n.text == "&&" || n.text == "||" || n.text == "??") return right;
        return session.binary(n.span, n.text, left, right);
    }
    case Kind::call: {
        auto target = evaluate(n.children[0], env);
        std::vector<RuntimeValue> args;
        args.reserve(n.children.size() - 1);
        session.allocate(n.span, n.children.size() - 1, sizeof(RuntimeValue));
        for (std::size_t i = 1; i < n.children.size(); ++i) args.push_back(evaluate(n.children[i], env));
        return call(n.span, target, args);
    }
    default: session.fail(n.span, "DS_INTERNAL", "Statement used as an expression");
    }
}
RuntimeValue Interpreter::call(SourceSpan span, const RuntimeValue& target, const std::vector<RuntimeValue>& args) {
    session.tick(span);
    if (const auto* host = std::get_if<HostReference>(&target)) return session.call_host(span, *host, args);
    const auto* function = std::get_if<ScriptFunction>(&target);
    if (!function) session.fail(span, "DS_CALL", "Attempted to call a non-callable value");
    const auto& callable = session.callables.at(function->id);
    Session::Frame frame(session, span, callable.name);
    const auto env = session.prepare_call(span, callable, args);
    auto completion = scope(callable.node, env);
    return completion.kind == CompletionKind::returned ? completion.value : RuntimeValue(Value{});
}
RunResult Interpreter::run() {
    RunResult result;
    const auto& root = node(program.root);
    try {
        const auto globals = session.make_env(root.span, std::nullopt);
        auto builtin = [&](std::string name, RuntimeValue value) {
            auto& binding = session.declare(root.span, globals, std::move(name), true);
            binding.value = std::move(value); binding.initialized = true;
        };
        builtin("undefined", Value{});
        builtin("NaN", Value(std::numeric_limits<double>::quiet_NaN()));
        builtin("Infinity", Value(std::numeric_limits<double>::infinity()));
        for (const auto& [name, host] : session.hosts) builtin(name, HostReference{&host});
        session.tick(root.span);
        auto completion = execute(program.root, session.make_env(root.span, globals));
        const auto* value = std::get_if<Value>(&completion.value);
        if (!value) session.fail(root.span, "DS_BOUNDARY", "Function values cannot escape an evaluation");
        result.value = *value;
    } catch (const RuntimeFailure& failure) { result.diagnostics.push_back(failure.diagnostic); }
    result.steps = session.steps; result.heap_bytes = session.heap;
    return result;
}
} // namespace
RunResult interpret(const Program& program, const Hosts& hosts, const EngineOptions& engine_options, RunOptions options) {
    return Interpreter(program, hosts, engine_options, options).run();
}
} // namespace dragscript::detail