#pragma once
#include "Module.hpp"
#include <iomanip>
#include <sstream>

namespace firestone::vm {

inline std::string format_value(const Value& value) {
    std::ostringstream out;
    switch (kind(value)) {
    case ValueKind::Null: out << "null"; break;
    case ValueKind::Boolean: out << (require<bool>(value) ? "true" : "false"); break;
    case ValueKind::Integer: out << require<std::int64_t>(value); break;
    case ValueKind::Float: out << std::setprecision(17) << require<double>(value); break;
    case ValueKind::String: out << std::quoted(require<std::string>(value)); break;
    case ValueKind::Address: out << "address(" << require<Address>(value).slot << ')'; break;
    case ValueKind::Object: out << "object(" << require<ObjectRef>(value).id << ')'; break;
    }
    return out.str();
}
inline std::string disassemble(const Module& module) {
    std::ostringstream out;
    for (Index n = 0; n < module.constants.size(); ++n) out << "constant " << n << " = " << format_value(module.constants.at(n)) << '\n';
    for (Index f = 0; f < module.size(); ++f) {
        const auto& fn = module.function(f);
        out << "function " << f << ' ' << std::quoted(fn.name) << " [" << (fn.model == VMModel::Register ? "register" : "stack") << "] -> " << kind_name(fn.signature.result) << '\n';
        for (Index pc = 0; pc < fn.code.size(); ++pc) {
            const auto& i = fn.code.at(pc); out << "  " << pc << ": ";
            if (i.destination != no_index) out << '%' << i.destination << " = ";
            out << opcode_info(i.opcode).name;
            for (auto operand : i.operands) out << " %" << operand;
            if (i.index != no_index) out << " index=" << i.index;
            if (i.target != no_index) out << " ->" << i.target;
            if (i.alternate != no_index) out << ',' << i.alternate;
            if (!i.symbol.empty()) out << ' ' << std::quoted(i.symbol);
            if (i.opcode == Opcode::Call || i.opcode == Opcode::NativeCall || i.opcode == Opcode::Return) out << " argc=" << i.argument_count;
            if (arithmetic(i.opcode)) out << " bits=" << i.bits;
            out << '\n';
        }
    }
    return out.str();
}

} // namespace firestone::vm
