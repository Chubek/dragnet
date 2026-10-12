#pragma once
#include "Imperative.hpp"

namespace firestone::type {

using TypeSubstitution = std::map<std::string, Type>;

inline Type apply_substitution(const Type& type, const TypeSubstitution& substitution) {
    validate(type, true);
    std::set<std::string> active;
    std::function<Type(const Type&)> visit = [&](const Type& current) {
        if (current.kind == Kind::Variable && substitution.count(current.name)) {
            if (!active.insert(current.name).second) throw std::invalid_argument("cyclic type substitution");
            auto result = visit(substitution.at(current.name)); active.erase(current.name); return result;
        }
        auto result = current;
        for (auto& part : result.parts) part = visit(part);
        return result;
    };
    auto result = visit(type); validate(result, true); return result;
}

inline bool occurs(const std::string& variable, const Type& type) {
    if (type.kind == Kind::Variable && type.name == variable) return true;
    for (const auto& part : type.parts) if (occurs(variable, part)) return true;
    return false;
}

inline std::optional<TypeSubstitution> unify(const Type& left, const Type& right) {
    validate(left, true); validate(right, true);
    TypeSubstitution substitution;
    std::vector<std::pair<Type, Type>> pending{{left, right}};
    while (!pending.empty()) {
        auto pair = pending.back(); pending.pop_back();
        auto a = apply_substitution(pair.first, substitution), b = apply_substitution(pair.second, substitution);
        if (a == b) continue;
        if (b.kind == Kind::Variable && a.kind != Kind::Variable) std::swap(a, b);
        if (a.kind == Kind::Variable) {
            if (occurs(a.name, b)) return {};
            substitution[a.name] = std::move(b); continue;
        }
        if (a.kind != b.kind || a.scalar != b.scalar || a.name != b.name || a.count != b.count ||
            a.fields != b.fields || a.parts.size() != b.parts.size()) return {};
        for (std::size_t i = 0; i < a.parts.size(); ++i) pending.emplace_back(a.parts[i], b.parts[i]);
    }
    for (auto& binding : substitution) binding.second = apply_substitution(binding.second, substitution);
    return substitution;
}

} // namespace firestone::type
