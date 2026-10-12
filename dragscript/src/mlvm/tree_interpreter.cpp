#include "lowering.hpp"
#include "runtime/session.hpp"
#include <charconv>
#include <limits>

namespace dragscript::detail {
namespace {
// Second execution backend: it dispatches verified MLVM ds.* operations instead of
// DSIR nodes. Values, coercions, scopes, host calls and budgets come from Session, so
// only construct dispatch is written twice. Equivalence between the backends is a
// tested property, not an assumption.
class TreeInterpreter {
public:
    TreeInterpreter(const LoweredModule& lowered, const Program& program, const Hosts& hosts,
                    const EngineOptions& limits, RunOptions options)
        : lowered(lowered), program(program), session(program.source, hosts, limits, options) {}
    RunResult run();
private:
    const LoweredModule& lowered;
    const Program& program;
    Session session;
    static std::string attribute(const mlvm::Operation& op, const std::string& name) {
        const auto it = op.attributes.find(name);
        return it == op.attributes.end() ? std::string{} : it->second;
    }
    static bool flag(const mlvm::Operation& op, const std::string& name) { return attribute(op, name) == "true"; }
    static std::size_t number_attribute(const mlvm::Operation& op, const char* name) {
        std::size_t value = 0;
        const auto text = attribute(op, name);
        if (!text.empty()) std::from_chars(text.data(), text.data() + text.size(), value);
        return value;
    }
    std::vector<std::string> indexed(const mlvm::Operation& op, const std::string& key) const {
        std::vector<std::string> result;
        for (std::size_t index = 0;; ++index) {
            auto text = attribute(op, key + "_" + std::to_string(index));
            if (text.empty()) break;
            result.push_back(std::move(text));
        }
        return result;
    }
    SourceSpan span(const mlvm::Operation& op) const {
        return source_span(program.source, number_attribute(op, "source_begin"), number_attribute(op, "source_end"));
    }
    // The dialect verifier guarantees one semantic operation plus its terminating yield.
    const mlvm::Operation& child(const mlvm::Operation& op, std::size_t index) const {
        return op.regions[index].blocks[0].operations.front();
    }
    RuntimeValue child_value(const mlvm::Operation& op, std::size_t index, EnvId env) { return evaluate(child(op, index), env); }
    Completion child_completion(const mlvm::Operation& op, std::size_t index, EnvId env) { return execute(child(op, index), env); }
    Value literal(const mlvm::Operation& op);
    void prepare(const mlvm::Operation& op, EnvId env);
    Completion scope(const mlvm::Operation& op, EnvId env);
    Completion execute(const mlvm::Operation& op, EnvId env);
    RuntimeValue evaluate(const mlvm::Operation& op, EnvId env);
    RuntimeValue call(SourceSpan where, const RuntimeValue& target, const std::vector<RuntimeValue>& args);
    // A statement child yields its value through the completion record.
    bool truthy_completion(const Completion& completion) {
        return completion.has_value && session.truthy(completion.value);
    }
    Binding& reference(SourceSpan where, EnvId env, const std::string& name) {
        auto* binding = session.lookup(env, name);
        if (!binding) session.fail(where, "DS_REFERENCE", "Unknown identifier: " + name);
        if (!binding->initialized) session.fail(where, "DS_TDZ", "Binding accessed before initialization: " + name);
        return *binding;
    }
};
Value TreeInterpreter::literal(const mlvm::Operation& op) {
    const auto tag = attribute(op, "tag"), text = attribute(op, "payload");
    if (tag == "undefined") return {};
    if (tag == "null") return Value::null();
    if (tag == "boolean") return Value(text == "true");
    if (tag == "number") {
        double number = 0;
        std::from_chars(text.data(), text.data() + text.size(), number);
        return Value(number);
    }
    if (tag == "string") {
        std::u16string units; units.reserve(text.size() / 4);
        for (std::size_t i = 0; i + 3 < text.size(); i += 4) {
            unsigned unit = 0;
            for (unsigned digit = 0; digit < 4; ++digit) {
                const char c = text[i + digit];
                unit = unit * 16 + static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + 10);
            }
            units.push_back(static_cast<char16_t>(unit));
        }
        return Value(std::move(units));
    }
    session.fail(span(op), "DS_INTERNAL", "Unknown literal tag");
}
void TreeInterpreter::prepare(const mlvm::Operation& op, EnvId env) {
    for (const auto& region : op.regions) {
        const auto& statement = region.blocks[0].operations.front();
        const auto where = span(statement);
        if (statement.name == "ds.sequence") { prepare(statement, env); continue; }
        if (statement.name == "ds.declaration") {
            session.declare(where, env, attribute(statement, "text"), flag(statement, "immutable"));
        } else if (statement.name == "ds.function") {
            Callable callable;
            callable.closure = env;
            const auto name = attribute(statement, "text"); // Read before the callable is moved.
            callable.name = name;
            callable.parameters = indexed(statement, "parameter");
            callable.operation = &statement.regions[0].blocks[0].operations.front();
            const auto id = session.add_callable(std::move(callable));
            auto& binding = session.declare(where, env, name, false);
            binding.value = ScriptFunction{id}; binding.initialized = true;
        }
    }
}
Completion TreeInterpreter::scope(const mlvm::Operation& op, EnvId env) {
    prepare(op, env);
    Completion result;
    for (const auto& region : op.regions) {
        auto next = execute(region.blocks[0].operations.front(), env);
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
Completion TreeInterpreter::execute(const mlvm::Operation& op, EnvId env) {
    Session::Depth depth(session, span(op));
    const auto where = span(op);
    if (op.name == "ds.program") return scope(op, env);
    if (op.name == "ds.block") return scope(op, session.make_env(where, env));
    if (op.name == "ds.sequence") {
        for (const auto& region : op.regions) execute(region.blocks[0].operations.front(), env);
        return {};
    }
    if (op.name == "ds.empty" || op.name == "ds.function") return {};
    if (op.name == "ds.expression") return normal_completion(child_value(op, 0, env));
    if (op.name == "ds.declaration") {
        auto* target = session.lookup(env, attribute(op, "text"));
        if (!target) session.fail(where, "DS_INTERNAL", "Declaration was not prepared");
        auto& binding = *target;
        binding.value = op.regions.empty() ? RuntimeValue(Value{}) : child_value(op, 0, env);
        binding.initialized = true;
        return {};
    }
    if (op.name == "ds.if") {
        if (session.truthy(child_value(op, 0, env))) {
            auto result = child_completion(op, 1, env);
            result.has_value = true;
            return result;
        }
        if (op.regions.size() == 3) {
            auto result = child_completion(op, 2, env);
            result.has_value = true;
            return result;
        }
        return normal_completion(Value{});
    }
    if (op.name == "ds.return")
        return {CompletionKind::returned, op.regions.empty() ? RuntimeValue(Value{}) : child_value(op, 0, env), true};
    if (op.name == "ds.break") return abrupt_completion(CompletionKind::broke);
    if (op.name == "ds.continue") return abrupt_completion(CompletionKind::continued);
    if (op.name == "ds.while" || op.name == "ds.do") {
        const bool is_while = op.name == "ds.while";
        const auto test = is_while ? 0 : 1, body = is_while ? 1 : 0;
        Completion result;
        auto step = [&] {
            auto iteration = child_completion(op, body, env);
            if (iteration.kind == CompletionKind::returned) return iteration;
            // A loop's completion is the completion of its last iteration.
            result = absorbed(iteration);
            return iteration;
        };
        if (!is_while && step().kind == CompletionKind::returned) return result; // do-while runs its body first.
        // while evaluates its test before every iteration, including the first.
        while (session.truthy(child_value(op, test, env))) {
            const auto iteration = step();
            if (iteration.kind == CompletionKind::returned) return iteration;
            if (iteration.kind == CompletionKind::broke) break;
        }
        return result;
    }
    if (op.name == "ds.for") {
        const auto env_for = session.make_env(where, env);
        std::size_t cursor = 0;
        if (flag(op, "init")) { // Loop bindings are prepared before their initializer runs.
            if (!indexed(op, "binding").empty()) prepare(child(op, cursor), env_for);
            execute(child(op, cursor), env_for);
            ++cursor;
        }
        const auto test = cursor;
        if (flag(op, "test")) ++cursor;
        const auto update = cursor;
        if (flag(op, "update")) ++cursor;
        const auto body = cursor;
        const auto bindings = indexed(op, "binding");
        EnvId current = env_for;
        Completion result;
        while (!flag(op, "test") || truthy_completion(execute(child(op, test), current))) {
            EnvId iteration_env = current;
            if (!bindings.empty()) { // Per-iteration bindings copy the previous iteration's values.
                iteration_env = session.make_env(where, env_for);
                for (const auto& binding : bindings) {
                    auto* source = session.lookup(current, binding);
                    auto& target = session.declare(where, iteration_env, binding, false);
                    target.value = source ? source->value : RuntimeValue(Value{});
                    target.initialized = true;
                }
            }
            auto iteration = execute(child(op, body), iteration_env);
            if (iteration.kind == CompletionKind::returned) return iteration;
            if (iteration.kind == CompletionKind::broke) { result = absorbed(iteration); break; }
            result = absorbed(iteration); // A loop's completion is its last iteration's completion.
            if (flag(op, "update")) execute(child(op, update), iteration_env);
            current = iteration_env;
        }
        return result;
    }
    if (op.name == "ds.for_of") {
        auto iterable = child_value(op, 0, env);
        std::vector<RuntimeValue> items;
        if (const auto* array = std::get_if<ArrayId>(&iterable)) items = session.arrays[array->index].elements;
        else if (const auto* p = std::get_if<Value>(&iterable)) {
            if (p->tag() != Value::Tag::string) session.fail(where, "DS_TYPE", "for-of requires an array or string");
            const auto& text = p->string(); // Iteration is by code point, not code unit.
            for (std::size_t i = 0; i < text.size();) {
                std::size_t length = 1;
                if (text[i] >= 0xd800 && text[i] < 0xdc00 && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] < 0xe000)
                    length = 2;
                items.push_back(Value(text.substr(i, length))); i += length;
            }
        } else session.fail(where, "DS_TYPE", "for-of requires an array or string");
        const auto& variable = attribute(op, "text");
        Completion result;
        for (auto& item : items) {
            EnvId iteration_env = env;
            if (flag(op, "declared")) {
                iteration_env = session.make_env(where, env);
                auto& binding = session.declare(where, iteration_env, variable, flag(op, "immutable"));
                binding.value = std::move(item); binding.initialized = true;
            } else {
                auto& binding = reference(where, env, variable);
                if (binding.immutable) session.fail(where, "DS_CONST", "Assignment to immutable binding: " + variable);
                binding.value = std::move(item);
            }
            auto iteration = child_completion(op, 1, iteration_env);
            if (iteration.kind == CompletionKind::returned) return iteration;
            if (iteration.kind == CompletionKind::broke) { result = absorbed(iteration); break; }
            result = absorbed(iteration);
        }
        return result;
    }
    session.fail(where, "DS_INTERNAL", "Unknown statement operation: " + op.name);
}
RuntimeValue TreeInterpreter::evaluate(const mlvm::Operation& op, EnvId env) {
    Session::Depth depth(session, span(op));
    const auto where = span(op);
    const auto& text = attribute(op, "text");
    if (op.name == "ds.literal") {
        auto value = literal(op); // A literal compiled under laxer limits still obeys the run's quota.
        session.check_string(where, value);
        return value;
    }
    if (op.name == "ds.identifier") return reference(where, env, text).value;
    if (op.name == "ds.assign") {
        auto value = child_value(op, 0, env);
        auto& binding = reference(where, env, text);
        if (binding.immutable) session.fail(where, "DS_CONST", "Assignment to immutable binding: " + text);
        binding.value = value;
        return value;
    }
    if (op.name == "ds.member" || op.name == "ds.assign_member") {
        auto base = child_value(op, 0, env);
        auto key = session.to_key(where, child_value(op, 1, env));
        if (op.name == "ds.member") return session.get_property(where, base, key);
        auto value = child_value(op, 2, env);
        session.set_property(where, base, key, value);
        return value;
    }
    if (op.name == "ds.object") {
        const auto object = session.new_object(where);
        for (const auto& region : op.regions) { // Keys and values evaluate left-to-right in source order.
            const auto& property = region.blocks[0].operations.front();
            auto key = session.to_key(where, evaluate(child(property, 0), env));
            session.set_own_property(where, object, key, evaluate(child(property, 1), env));
        }
        return object;
    }
    if (op.name == "ds.array") {
        const auto array = session.new_array(where);
        for (const auto& region : op.regions) session.array_push(where, array, evaluate(region.blocks[0].operations.front(), env));
        return array;
    }
    if (op.name == "ds.update") {
        const auto& increment = attribute(op, "increment");
        auto& binding = reference(where, env, text);
        if (binding.immutable) session.fail(where, "DS_CONST", "Assignment to immutable binding: " + text);
        const auto previous = session.to_number(where, binding.value);
        const auto next = increment == "++" ? previous + 1 : previous - 1;
        binding.value = Value(next);
        return Value(flag(op, "prefix") ? next : previous);
    }
    if (op.name == "ds.compound") {
        auto& current = reference(where, env, text);
        auto right = child_value(op, 1, env);
        auto value = session.binary(where, attribute(op, "operator"), current.value, right);
        if (current.immutable) session.fail(where, "DS_CONST", "Assignment to immutable binding: " + text);
        current.value = value;
        return value;
    }
    if (op.name == "ds.unary") {
        if (text == "typeof") {
            const auto& operand = child(op, 0);
            if (operand.name == "ds.identifier" && !session.lookup(env, attribute(operand, "text")))
                return Value::text(u"undefined"); // Unresolvable typeof is not a ReferenceError.
            auto target = evaluate(operand, env);
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
        if (text == "void") { child_value(op, 0, env); return Value{}; }
        auto value = child_value(op, 0, env);
        if (text == "!") return Value(!session.truthy(value));
        const auto number = session.to_number(where, value);
        return Value(text == "-" ? -number : number);
    }
    if (op.name == "ds.binary") {
        auto left = child_value(op, 0, env);
        if (text == "&&" && !session.truthy(left)) return left;
        if (text == "||" && session.truthy(left)) return left;
        if (text == "??" && !is_nullish(left)) return left;
        auto right = child_value(op, 1, env);
        if (text == "&&" || text == "||" || text == "??") return right;
        return session.binary(where, text, left, right);
    }
    if (op.name == "ds.call") {
        auto target = child_value(op, 0, env);
        std::vector<RuntimeValue> args;
        session.allocate(where, op.regions.size() - 1, sizeof(RuntimeValue));
        args.reserve(op.regions.size() - 1);
        for (std::size_t i = 1; i < op.regions.size(); ++i) args.push_back(child_value(op, i, env));
        return call(where, target, args);
    }
    session.fail(where, "DS_INTERNAL", "Unknown expression operation: " + op.name);
}
RuntimeValue TreeInterpreter::call(SourceSpan where, const RuntimeValue& target, const std::vector<RuntimeValue>& args) {
    session.tick(where);
    if (const auto* host = std::get_if<HostReference>(&target)) return session.call_host(where, *host, args);
    const auto* function = std::get_if<ScriptFunction>(&target);
    if (!function) session.fail(where, "DS_CALL", "Attempted to call a non-callable value");
    const auto& callable = session.callables.at(function->id);
    Session::Frame frame(session, where, callable.name);
    const auto env = session.prepare_call(where, callable, args);
    const auto& body = *static_cast<const mlvm::Operation*>(callable.operation);
    Session::Depth depth(session, span(body));
    auto completion = scope(body, env);
    return completion.kind == CompletionKind::returned ? completion.value : RuntimeValue(Value{});
}
RunResult TreeInterpreter::run() {
    RunResult result;
    const auto& root = program.nodes[program.root];
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
        const auto& program_op = lowered.module.body.blocks[0].operations.front();
        auto completion = execute(program_op, session.make_env(root.span, globals));
        const auto* value = std::get_if<Value>(&completion.value);
        if (!value) session.fail(root.span, "DS_BOUNDARY", "Function values cannot escape an evaluation");
        result.value = *value;
    } catch (const RuntimeFailure& failure) { result.diagnostics.push_back(failure.diagnostic); }
    result.steps = session.steps; result.heap_bytes = session.heap;
    return result;
}
} // namespace
RunResult interpret_mlvm(const LoweredModule& lowered, const Program& program, const Hosts& hosts,
                         const EngineOptions& engine_options, RunOptions options) {
    return TreeInterpreter(lowered, program, hosts, engine_options, options).run();
}
} // namespace dragscript::detail