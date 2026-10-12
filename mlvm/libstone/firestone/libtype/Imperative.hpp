#pragma once
#include "Atomic.hpp"

namespace firestone::type {

enum class Kind { Atomic, Pointer, Array, Record, Function, Variable };

// Finite structural types. Function parts are (result, parameters...).
struct Type {
    Kind kind = Kind::Atomic;
    Atomic scalar;
    std::string name;
    std::size_t count = 0;
    std::vector<Type> parts;
    std::vector<std::string> fields;

    static Type atomic(Atomic scalar) { Type t; t.scalar = std::move(scalar); return t; }
    static Type pointer(Type pointee) { Type t; t.kind = Kind::Pointer; t.parts.push_back(std::move(pointee)); return t; }
    static Type array(Type element, std::size_t count) {
        Type t; t.kind = Kind::Array; t.count = count; t.parts.push_back(std::move(element)); return t;
    }
    static Type record(std::vector<std::pair<std::string, Type>> fields, std::string name = {}) {
        Type t; t.kind = Kind::Record; t.name = std::move(name);
        std::set<std::string> seen;
        for (auto& field : fields) {
            if (field.first.empty() || !seen.insert(field.first).second) throw std::invalid_argument("invalid record field name");
            t.fields.push_back(std::move(field.first)); t.parts.push_back(std::move(field.second));
        }
        return t;
    }
    static Type function(Type result, std::vector<Type> parameters = {}) {
        Type t; t.kind = Kind::Function; t.parts.push_back(std::move(result));
        t.parts.insert(t.parts.end(), parameters.begin(), parameters.end()); return t;
    }
    static Type variable(std::string name) {
        if (name.empty()) throw std::invalid_argument("empty type variable");
        Type t; t.kind = Kind::Variable; t.name = std::move(name); return t;
    }
    friend bool operator==(const Type& a, const Type& b) {
        return a.kind == b.kind && a.scalar == b.scalar && a.name == b.name && a.count == b.count &&
               a.parts == b.parts && a.fields == b.fields;
    }
    friend bool operator!=(const Type& a, const Type& b) { return !(a == b); }
};

inline void validate(const Type& type, bool allow_void = false) {
    if (type.kind != Kind::Atomic && type.scalar != Atomic{}) throw std::invalid_argument("nonatomic type has scalar metadata");
    if (type.kind != Kind::Record && !type.fields.empty()) throw std::invalid_argument("unexpected field metadata");
    if (type.kind != Kind::Array && type.count) throw std::invalid_argument("unexpected array metadata");
    if (type.kind != Kind::Variable && type.kind != Kind::Record && !type.name.empty()) throw std::invalid_argument("unexpected type name");
    switch (type.kind) {
    case Kind::Atomic:
        if (!type.parts.empty() || !valid_atomic(type.scalar, allow_void)) throw std::invalid_argument("invalid atomic type");
        break;
    case Kind::Pointer: case Kind::Array:
        if (type.parts.size() != 1) throw std::invalid_argument("type requires one element");
        validate(type.parts[0], type.kind == Kind::Pointer); break;
    case Kind::Record: {
        if (type.fields.size() != type.parts.size()) throw std::invalid_argument("record field/type counts differ");
        std::set<std::string> seen;
        for (std::size_t i = 0; i < type.parts.size(); ++i) {
            if (type.fields[i].empty() || !seen.insert(type.fields[i]).second) throw std::invalid_argument("invalid record field");
            validate(type.parts[i]);
        }
        break;
    }
    case Kind::Function:
        if (type.parts.empty()) throw std::invalid_argument("function requires a return type");
        validate(type.parts[0], true);
        for (std::size_t i = 1; i < type.parts.size(); ++i) validate(type.parts[i]);
        break;
    case Kind::Variable:
        if (type.name.empty() || !type.parts.empty()) throw std::invalid_argument("invalid type variable");
        break;
    default: throw std::invalid_argument("invalid structural type kind");
    }
}

} // namespace firestone::type
