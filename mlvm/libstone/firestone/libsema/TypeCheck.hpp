#pragma once
#include "CallCheck.hpp"
#include "../libir/SimpleSSAGraph.hpp"
#include "../libir/CPSGraph.hpp"
#include "../libir/Typed3AC.hpp"

namespace firestone::sema {

inline ir::Diagnostics check_types(const ir::Instruction& instruction, const std::function<ir::Type(ir::ValueId)>& resolve) {
    return ir::verify_instruction(instruction, resolve);
}
inline ir::Diagnostics check_types(const ir::Typed3AC& program) { return program.verify(); }
inline ir::Diagnostics check_types(const ir::CPSGraph& program) { return program.verify(); }
inline ir::Diagnostics check_types(const ir::SimpleSSAGraph& program) { return program.verify(); }
inline ir::Diagnostics check_types(const ir::SimpleSSAGraph& program, const FunctionSignatures& signatures) {
    auto result = program.verify();
    auto more = check_calls(program.cfg(), signatures, [&](ir::ValueId id) { return id < program.value_count() ? program.type(id) : ir::Type{}; });
    result.insert(result.end(), more.begin(), more.end()); return result;
}

} // namespace firestone::sema
