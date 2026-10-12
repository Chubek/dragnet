#pragma once
#include "Imperative.hpp"
#include <iomanip>
#include <sstream>

namespace firestone::type {

inline std::string to_string(const Type& type) {
    validate(type, true);
    if (type.kind == Kind::Atomic) return atomic_name(type.scalar);
    if (type.kind == Kind::Variable) return "'" + type.name;
    if (type.kind == Kind::Pointer) return "ptr<" + to_string(type.parts[0]) + ">";
    if (type.kind == Kind::Array) return "[" + std::to_string(type.count) + " x " + to_string(type.parts[0]) + "]";
    std::ostringstream out;
    if (type.kind == Kind::Function) {
        out << "fn(";
        for (std::size_t i = 1; i < type.parts.size(); ++i) { if (i > 1) out << ", "; out << to_string(type.parts[i]); }
        out << ") -> " << to_string(type.parts[0]);
    } else {
        out << "record " << std::quoted(type.name) << " {";
        for (std::size_t i = 0; i < type.parts.size(); ++i) {
            if (i) out << ", ";
            out << std::quoted(type.fields[i]) << ": " << to_string(type.parts[i]);
        }
        out << '}';
    }
    return out.str();
}

} // namespace firestone::type
