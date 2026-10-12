#pragma once
#include "Object.hpp"
#include <functional>
#include <optional>

namespace firestone::vm {

using NativeBody = std::function<Value(const std::vector<Value>&)>;
struct NativeFunction { Signature signature; NativeBody body; };

class Environment {
public:
    void bind(std::string name, Signature signature, NativeBody body) {
        if (name.empty() || !body || !valid_kind(signature.result)) throw std::invalid_argument("invalid native binding");
        for (auto parameter : signature.parameters) if (!valid_kind(parameter)) throw std::invalid_argument("invalid native parameter kind");
        natives_.insert_or_assign(std::move(name), NativeFunction{std::move(signature), std::move(body)});
    }
    const NativeFunction& native(const std::string& name) const {
        auto found = natives_.find(name);
        if (found == natives_.end()) throw VMError("vm.native", "unbound native symbol: " + name);
        return found->second;
    }
    Value call(const std::string& name, const Signature& expected, const std::vector<Value>& arguments) const {
        // Copy before invoking: a callback may rebind this environment.
        auto function = native(name);
        if (!(function.signature == expected)) throw VMError("vm.signature", "native binding signature mismatch");
        check_arguments(expected, arguments);
        auto result = function.body(arguments);
        check_result(expected, result);
        return result;
    }
    void set_global(std::string name, Value value) {
        if (name.empty()) throw std::invalid_argument("empty global name");
        globals_.insert_or_assign(std::move(name), std::move(value));
    }
    const Value& global(const std::string& name) const {
        auto found = globals_.find(name);
        if (found == globals_.end()) throw VMError("vm.global", "unbound global: " + name);
        return found->second;
    }
    Address allocate(Value value = {}) { memory_.push_back(std::move(value)); return {memory_.size() - 1}; }
    const Value& load(Address address) const {
        if (address.slot >= memory_.size() || !memory_[address.slot]) throw VMError("vm.address", "invalid or released heap address");
        return *memory_[address.slot];
    }
    void store(Address address, Value value) { load(address); memory_[address.slot] = std::move(value); }
    void release(Address address) { load(address); memory_[address.slot].reset(); }
    ObjectRef create_object(MetaObject meta, std::map<std::string, Value> fields) {
        if (meta.name.empty() || fields.size() != meta.fields.size()) throw std::invalid_argument("invalid object schema");
        for (const auto& field : meta.fields) {
            auto found = fields.find(field.first);
            if (!valid_kind(field.second) || found == fields.end() || kind(found->second) != field.second)
                throw std::invalid_argument("object field kind mismatch");
        }
        objects_.push_back({std::move(meta), std::move(fields)});
        return {objects_.size() - 1};
    }
    const Object& object(ObjectRef ref) const {
        if (ref.id >= objects_.size()) throw VMError("vm.object", "invalid object reference");
        return objects_[ref.id];
    }
    const Value& field(ObjectRef ref, const std::string& name) const {
        const auto& fields = object(ref).fields;
        auto found = fields.find(name);
        if (found == fields.end()) throw VMError("vm.field", "unknown object field: " + name);
        return found->second;
    }
    void set_field(ObjectRef ref, const std::string& name, Value value) {
        field(ref, name);
        if (kind(value) != object(ref).meta.fields.at(name)) throw VMError("vm.type", "object field kind mismatch");
        objects_[ref.id].fields.at(name) = std::move(value);
    }
    Value invoke(ObjectRef ref, const std::string& method, const std::vector<Value>& arguments) const {
        const auto& methods = object(ref).meta.methods;
        auto found = methods.find(method);
        if (found == methods.end()) throw VMError("vm.method", "unknown object method");
        auto symbol = found->second;
        std::vector<Value> actual{ref}; actual.insert(actual.end(), arguments.begin(), arguments.end());
        auto signature = native(symbol).signature;
        return call(symbol, signature, actual);
    }
private:
    std::map<std::string, NativeFunction> natives_;
    std::map<std::string, Value> globals_;
    std::vector<std::optional<Value>> memory_;
    std::vector<Object> objects_;
};

} // namespace firestone::vm
