#pragma once
#include "Untyped3AC.hpp"

namespace firestone::ir {

class Typed3AC : public Untyped3AC {
public:
    Diagnostics verify() const { return verify_impl(true); }
    ValueId compute(Opcode opcode, Type type, std::vector<Operand> operands, std::string annotation = {}) {
        if (is_terminator(opcode) || opcode == Opcode::Store || opcode == Opcode::Phi || type.kind == TypeKind::Void)
            throw std::invalid_argument("compute requires a value-producing linear instruction");
        auto value = create_register(type);
        emit({opcode, value, std::move(type), std::move(operands), {}, {}, std::move(annotation)});
        return value;
    }
};

} // namespace firestone::ir
