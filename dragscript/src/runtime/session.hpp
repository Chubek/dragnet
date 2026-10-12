#pragma once
#include "semantic/dsir.hpp"

namespace dragscript::detail {
// A run arena index. Handles are checked indices, never raw pointers exposed to script.
using EnvId = std::size_t;
// Distinct strong types keep objects, arrays and callables distinguishable in ===.
struct ObjectId {
    std::size_t index = 0;
    friend bool operator==(const ObjectId&, const ObjectId&) = default;
};
struct ArrayId {
    std::size_t index = 0;
    friend bool operator==(const ArrayId&, const ArrayId&) = default;
};
struct ScriptFunction { std::size_t id; };
struct HostReference { const HostBinding* binding; };
using RuntimeValue = std::variant<Value, ScriptFunction, HostReference, ObjectId, ArrayId>;

enum class CompletionKind : std::uint8_t { normal, returned, broke, continued };
// An abrupt completion is distinct from a normal one, and empty from undefined-valued.
struct Completion {
    CompletionKind kind = CompletionKind::normal;
    RuntimeValue value = Value{};
    bool has_value = false;
};
inline Completion normal_completion(RuntimeValue value) { return {CompletionKind::normal, std::move(value), true}; }
inline Completion empty_completion() { return {}; }
inline Completion abrupt_completion(CompletionKind kind) { return {kind, Value{}, false}; }
// A loop absorbs break and continue: its own completion is normal again.
inline Completion absorbed(Completion completion) {
    completion.kind = CompletionKind::normal;
    return completion;
}

struct Binding { RuntimeValue value = Value{}; bool initialized = false; bool immutable = false; };
struct Environment {
    std::optional<EnvId> parent;
    std::map<std::string, Binding, std::less<>> bindings;
};
struct Property { std::u16string key; RuntimeValue value; };
struct Object { std::vector<Property> properties; };   // Insertion ordered; string keys only.
struct Array { std::vector<RuntimeValue> elements; };  // Dense; no holes.
struct Callable {
    EnvId closure = 0;
    std::string name;
    std::vector<std::string> parameters;
    NodeId node = 0;            // DSIR backend body.
    const void* operation = nullptr; // MLVM backend body (ds.block operation).
};
struct RuntimeFailure { Diagnostic diagnostic; };

// ECMAScript Number::toString for binary64, as UTF-16. Independent of any run arena.
std::u16string number_to_string(double value);

// Shared value, coercion, scope, host and budget services for every execution backend.
// The two backends differ only in how they dispatch source constructs.
class Session {
public:
    Session(const Source& source, const Hosts& hosts, const EngineOptions& limits, RunOptions options)
        : source(source), hosts(hosts), limits(limits), options(options) {}

    [[noreturn]] void fail(SourceSpan span, std::string code, std::string message, std::string limit = {}) {
        auto d = diagnostic(source, std::move(code), std::move(message), span, Phase::runtime, std::move(limit));
        d.trace = trace;
        throw RuntimeFailure{std::move(d)};
    }
    void tick(SourceSpan span) {
        if (cancelled()) fail(span, "DS_CANCELLED", "Execution cancelled");
        if (budget_exhausted || !consume(1)) fail(span, "DS_LIMIT", "Execution step limit exceeded", "steps");
    }
    struct Depth {
        Session& session; SourceSpan span;
        Depth(Session& session, SourceSpan span) : session(session), span(span) {
            session.tick(span);
            if (session.depth >= session.options.max_evaluation_depth)
                session.fail(span, "DS_LIMIT", "Evaluation nesting limit exceeded", "evaluation_depth");
            ++session.depth;
        }
        ~Depth() { --session.depth; }
    };
    bool cancelled() const { return options.cancellation && options.cancellation->load(std::memory_order_relaxed); }
    bool consume(std::uint64_t count) { // Host-visible charging; a false result ends the run.
        if (count > options.max_steps - steps) { budget_exhausted = true; return false; }
        steps += count; return true;
    }
    bool host_budget_exhausted() const noexcept { return budget_exhausted; }
    void allocate(SourceSpan span, std::size_t count, std::size_t size = 1) {
        if (size && (heap > options.max_heap_bytes || count > (options.max_heap_bytes - heap) / size))
            fail(span, "DS_LIMIT", "Runtime allocation quota exceeded", "heap_bytes");
        heap += count * size;
    }

    EnvId make_env(SourceSpan span, std::optional<EnvId> parent) {
        allocate(span, sizeof(Environment) + 64);
        auto env = std::make_unique<Environment>(); env->parent = parent;
        const auto id = environments.size(); environments.push_back(std::move(env)); return id;
    }
    Binding& declare(SourceSpan span, EnvId env, std::string name, bool immutable) {
        allocate(span, sizeof(Binding) + 128); allocate(span, name.size());
        auto [it, inserted] = environments[env]->bindings.emplace(std::move(name), Binding{});
        if (!inserted) fail(span, "DS_DUPLICATE", "Duplicate runtime binding");
        it->second.immutable = immutable; return it->second;
    }
    Binding* lookup(EnvId env, std::string_view name) {
        while (true) {
            auto it = environments[env]->bindings.find(name);
            if (it != environments[env]->bindings.end()) return &it->second;
            if (!environments[env]->parent) return nullptr;
            env = *environments[env]->parent;
        }
    }

    ObjectId new_object(SourceSpan span) {
        allocate(span, sizeof(Object));
        objects.emplace_back(); // Emplace before handing out the index.
        return ObjectId{objects.size() - 1};
    }
    ArrayId new_array(SourceSpan span) {
        allocate(span, sizeof(Array));
        arrays.emplace_back();
        return ArrayId{arrays.size() - 1};
    }
    // Insertion-ordered own properties; existing keys keep their position.
    void set_own_property(SourceSpan span, ObjectId object, const std::u16string& key, RuntimeValue value) {
        auto& properties = objects[object.index].properties;
        for (auto& property : properties)
            if (property.key == key) { property.value = std::move(value); return; }
        if (properties.size() >= limits.max_properties_per_object)
            fail(span, "DS_LIMIT", "Object property limit exceeded", "properties_per_object");
        if (key.size() > limits.max_property_name_units)
            fail(span, "DS_LIMIT", "Property name limit exceeded", "property_name_units");
        allocate(span, key.size() * sizeof(char16_t));
        properties.push_back({key, std::move(value)});
    }

    bool truthy(const RuntimeValue& value) const noexcept {
        if (const auto* p = std::get_if<Value>(&value)) return p->truthy();
        return true;
    }
    bool strict_equals(const RuntimeValue& a, const RuntimeValue& b) const noexcept {
        if (a.index() != b.index()) return false; // Never equates null with undefined.
        if (const auto* p = std::get_if<Value>(&a)) return p->strictly_equals(std::get<Value>(b));
        if (const auto* f = std::get_if<ScriptFunction>(&a)) return f->id == std::get<ScriptFunction>(b).id;
        if (const auto* h = std::get_if<HostReference>(&a)) return h->binding == std::get<HostReference>(b).binding;
        if (const auto* o = std::get_if<ObjectId>(&a)) return *o == std::get<ObjectId>(b);
        if (const auto* x = std::get_if<ArrayId>(&a)) return *x == std::get<ArrayId>(b);
        return false;
    }
    bool is_object(const RuntimeValue& value) const noexcept {
        return std::holds_alternative<ObjectId>(value) || std::holds_alternative<ArrayId>(value);
    }

    // Applies the string unit quota to any string that enters the runtime.
    void check_string(SourceSpan span, const Value& value) {
        if (value.tag() != Value::Tag::string) return;
        if (value.string().size() > limits.max_string_units)
            fail(span, "DS_LIMIT", "String size limit exceeded", "string_units");
        allocate(span, value.string().size(), sizeof(char16_t));
    }
    std::u16string to_string(SourceSpan span, const RuntimeValue& value);
    double to_number(SourceSpan span, const RuntimeValue& value);
    RuntimeValue to_primitive(SourceSpan span, const RuntimeValue& value, bool prefer_number);
    std::u16string to_key(SourceSpan span, const RuntimeValue& value) { return to_string(span, value); }

    // `==`-free binary contract; short-circuit and compound forms are handled here.
    RuntimeValue binary(SourceSpan span, std::string op, const RuntimeValue& left, const RuntimeValue& right);

    RuntimeValue get_property(SourceSpan span, const RuntimeValue& base, const std::u16string& key);
    void set_property(SourceSpan span, const RuntimeValue& base, const std::u16string& key, RuntimeValue value);
    // Dense arrays grow with undefined padding; string length is measured in code units.
    void array_push(SourceSpan span, ArrayId array, RuntimeValue value);
    void array_set(SourceSpan span, ArrayId array, std::size_t position, RuntimeValue value);

    struct Frame {
        Session& session;
        Frame(Session& session, SourceSpan span, std::string name) : session(session) {
            if (session.trace.size() >= session.options.max_call_depth)
                session.fail(span, "DS_LIMIT", "Call depth limit exceeded", "call_depth");
            session.trace.push_back({std::move(name), span});
        }
        ~Frame() { session.trace.pop_back(); }
    };
    EnvId prepare_call(SourceSpan span, const Callable& callable, const std::vector<RuntimeValue>& args) {
        const auto env = make_env(span, callable.closure);
        for (std::size_t i = 0; i < callable.parameters.size(); ++i) {
            auto& parameter = declare(span, env, callable.parameters[i], false);
            parameter.value = i < args.size() ? args[i] : RuntimeValue(Value{});
            parameter.initialized = true;
        }
        return env;
    }
    RuntimeValue call_host(SourceSpan span, HostReference host, const std::vector<RuntimeValue>& args);
    std::size_t add_callable(Callable callable) {
        allocate({}, sizeof(Callable)); const auto id = callables.size(); callables.push_back(std::move(callable)); return id;
    }

    const Source& source;
    const Hosts& hosts;
    const EngineOptions& limits;
    RunOptions options;
    std::vector<std::unique_ptr<Environment>> environments;
    std::vector<Object> objects;
    std::vector<Array> arrays;
    std::vector<Callable> callables;
    std::vector<TraceFrame> trace;
    std::uint64_t steps = 0;
    std::size_t heap = 0, depth = 0;
    bool budget_exhausted = false;
};

inline bool is_nullish(const RuntimeValue& value) {
    const auto* p = std::get_if<Value>(&value);
    return p && (p->tag() == Value::Tag::null || p->tag() == Value::Tag::undefined);
}
} // namespace dragscript::detail