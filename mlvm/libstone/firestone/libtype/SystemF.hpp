#pragma once
#include "Atomic.hpp"

namespace firestone::type {

enum class FKind { Atomic, Variable, Arrow, Forall };
struct FType {
    FKind kind = FKind::Variable;
    std::string name;
    Atomic scalar;
    std::vector<FType> parts;
    static FType atomic(Atomic type) { return {FKind::Atomic, {}, std::move(type), {}}; }
    static FType variable(std::string name) { return {FKind::Variable, std::move(name), {}, {}}; }
    static FType arrow(FType argument, FType result) { return {FKind::Arrow, {}, {}, {std::move(argument), std::move(result)}}; }
    static FType forall(std::string name, FType body) { return {FKind::Forall, std::move(name), {}, {std::move(body)}}; }
};

inline void validate(const FType& type) {
    if (type.kind != FKind::Atomic && type.scalar != Atomic{}) throw std::invalid_argument("unexpected System F scalar");
    switch (type.kind) {
    case FKind::Atomic:
        if (!valid_atomic(type.scalar) || !type.name.empty() || !type.parts.empty()) throw std::invalid_argument("invalid System F atomic type");
        break;
    case FKind::Variable:
        if (type.name.empty() || !type.parts.empty()) throw std::invalid_argument("invalid System F variable");
        break;
    case FKind::Arrow:
        if (!type.name.empty() || type.parts.size() != 2) throw std::invalid_argument("invalid System F arrow");
        break;
    case FKind::Forall:
        if (type.name.empty() || type.parts.size() != 1) throw std::invalid_argument("invalid universal type");
        break;
    default: throw std::invalid_argument("invalid System F kind");
    }
    for (const auto& part : type.parts) validate(part);
}

inline std::set<std::string> free_variables(const FType& type) {
    validate(type);
    std::set<std::string> result;
    if (type.kind == FKind::Variable) result.insert(type.name);
    for (const auto& part : type.parts) { auto names = free_variables(part); result.insert(names.begin(), names.end()); }
    if (type.kind == FKind::Forall) result.erase(type.name);
    return result;
}

inline bool alpha_equivalent(const FType& left, const FType& right) {
    validate(left); validate(right);
    std::vector<std::string> a_bound, b_bound;
    auto binding = [](const std::vector<std::string>& bound, const std::string& name) {
        for (std::size_t i = bound.size(); i > 0; --i) if (bound[i - 1] == name) return bound.size() - i;
        return ir::invalid_id;
    };
    std::function<bool(const FType&, const FType&)> equal = [&](const FType& a, const FType& b) {
        if (a.kind != b.kind || a.scalar != b.scalar) return false;
        if (a.kind == FKind::Variable) {
            auto ai = binding(a_bound, a.name), bi = binding(b_bound, b.name);
            return ai == bi && (ai != ir::invalid_id || a.name == b.name);
        }
        if (a.kind == FKind::Forall) { a_bound.push_back(a.name); b_bound.push_back(b.name); }
        bool result = true;
        for (std::size_t i = 0; i < a.parts.size(); ++i) result = equal(a.parts[i], b.parts[i]) && result;
        if (a.kind == FKind::Forall) { a_bound.pop_back(); b_bound.pop_back(); }
        return result;
    };
    return equal(left, right);
}

inline FType substitute(const FType& type, const std::string& variable, const FType& replacement) {
    validate(type); validate(replacement);
    if (type.kind == FKind::Variable) return type.name == variable ? replacement : type;
    if (type.kind == FKind::Forall && type.name == variable) return type;
    auto result = type;
    if (result.kind == FKind::Forall && free_variables(replacement).count(result.name) && free_variables(result.parts[0]).count(variable)) {
        std::set<std::string> names = free_variables(replacement);
        std::function<void(const FType&)> gather = [&](const FType& t) {
            if (!t.name.empty()) names.insert(t.name);
            for (const auto& part : t.parts) gather(part);
        };
        gather(type); names.insert(variable);
        auto fresh = result.name;
        do { fresh += "'"; } while (names.count(fresh));
        result.parts[0] = substitute(result.parts[0], result.name, FType::variable(fresh)); result.name = fresh;
    }
    for (auto& part : result.parts) part = substitute(part, variable, replacement);
    return result;
}

inline FType instantiate(const FType& universal, const FType& argument) {
    validate(universal);
    if (universal.kind != FKind::Forall) throw std::invalid_argument("type application requires forall");
    return substitute(universal.parts[0], universal.name, argument);
}

enum class FTermKind { Variable, Lambda, Apply, TypeLambda, TypeApply };
struct FTerm {
    FTermKind kind = FTermKind::Variable;
    std::string name;
    std::optional<FType> annotation;
    std::vector<FTerm> parts;
    static FTerm variable(std::string name) { return {FTermKind::Variable, std::move(name), {}, {}}; }
    static FTerm lambda(std::string name, FType type, FTerm body) { return {FTermKind::Lambda, std::move(name), std::move(type), {std::move(body)}}; }
    static FTerm apply(FTerm function, FTerm argument) { return {FTermKind::Apply, {}, {}, {std::move(function), std::move(argument)}}; }
    static FTerm type_lambda(std::string name, FTerm body) { return {FTermKind::TypeLambda, std::move(name), {}, {std::move(body)}}; }
    static FTerm type_apply(FTerm function, FType argument) { return {FTermKind::TypeApply, {}, std::move(argument), {std::move(function)}}; }
};

inline FType infer(const FTerm& term, std::map<std::string, FType> environment = {}) {
    switch (term.kind) {
    case FTermKind::Variable:
        if (term.name.empty() || term.annotation || !term.parts.empty()) throw std::invalid_argument("invalid term variable");
        if (!environment.count(term.name)) throw std::invalid_argument("unbound term variable: " + term.name);
        validate(environment.at(term.name)); return environment.at(term.name);
    case FTermKind::Lambda:
        if (term.name.empty() || !term.annotation || term.parts.size() != 1) throw std::invalid_argument("invalid lambda");
        validate(*term.annotation); environment[term.name] = *term.annotation;
        return FType::arrow(*term.annotation, infer(term.parts[0], std::move(environment)));
    case FTermKind::Apply: {
        if (!term.name.empty() || term.annotation || term.parts.size() != 2) throw std::invalid_argument("invalid application");
        auto function = infer(term.parts[0], environment), argument = infer(term.parts[1], environment);
        if (function.kind != FKind::Arrow || !alpha_equivalent(function.parts[0], argument)) throw std::invalid_argument("function argument type differs");
        return function.parts[1];
    }
    case FTermKind::TypeLambda:
        if (term.name.empty() || term.annotation || term.parts.size() != 1) throw std::invalid_argument("invalid type lambda");
        for (const auto& binding : environment) if (free_variables(binding.second).count(term.name))
            throw std::invalid_argument("type abstraction captures the environment");
        return FType::forall(term.name, infer(term.parts[0], std::move(environment)));
    case FTermKind::TypeApply:
        if (!term.name.empty() || !term.annotation || term.parts.size() != 1) throw std::invalid_argument("invalid type application");
        return instantiate(infer(term.parts[0], std::move(environment)), *term.annotation);
    }
    throw std::invalid_argument("invalid System F term kind");
}

} // namespace firestone::type
