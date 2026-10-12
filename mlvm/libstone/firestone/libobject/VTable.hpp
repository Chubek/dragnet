#pragma once
#include "../libir/Common.hpp"

namespace firestone::object {

struct Method {
    std::string name, target;
    ir::Type result = ir::Type::void_type();
    std::vector<ir::Type> parameters;
};

inline bool same_signature(const Method& a, const Method& b) { return a.result == b.result && a.parameters == b.parameters; }
inline void validate_method(const Method& method) {
    if (method.name.empty() || method.target.empty()) throw std::invalid_argument("method requires name and target");
    if (!method.result.valid_value() && method.result != ir::Type::void_type()) throw std::invalid_argument("invalid method return type");
    for (const auto& parameter : method.parameters) if (!parameter.valid_value()) throw std::invalid_argument("invalid method parameter type");
}

// Stable slots; overriding preserves the slot and exact call signature.
class VTable {
public:
    std::size_t install(Method method) {
        validate_method(method);
        auto found = slots_.find(method.name);
        if (found != slots_.end()) {
            if (!same_signature(methods_[found->second], method)) throw std::invalid_argument("override signature differs");
            methods_[found->second] = std::move(method); return found->second;
        }
        auto slot = methods_.size();
        methods_.push_back(std::move(method)); slots_[methods_.back().name] = slot; return slot;
    }
    bool contains(const std::string& name) const { return slots_.count(name) != 0; }
    std::size_t slot(const std::string& name) const { return slots_.at(name); }
    const Method& method(std::size_t slot) const { return methods_.at(slot); }
    const Method& method(const std::string& name) const { return method(slot(name)); }
    std::size_t size() const { return methods_.size(); }
private:
    std::vector<Method> methods_;
    std::map<std::string, std::size_t> slots_;
};

} // namespace firestone::object
