#pragma once
#include "Test.hpp"

namespace vm = firestone::vm;

inline vm::Instruction op(vm::Opcode opcode, std::vector<vm::Index> operands = {}, vm::Index destination = vm::no_index) {
    vm::Instruction i; i.opcode = opcode; i.operands = std::move(operands); i.destination = destination; return i;
}
inline vm::Instruction ret(std::vector<vm::Index> operands = {}, vm::Index count = 1) {
    auto i = op(vm::Opcode::Return, std::move(operands)); i.argument_count = count; return i;
}
inline vm::Module adder(vm::VMModel model = vm::VMModel::Register) {
    vm::Module module; vm::Function fn; fn.name = "add"; fn.model = model;
    fn.signature = {{vm::ValueKind::Integer, vm::ValueKind::Integer}, vm::ValueKind::Integer};
    if (model == vm::VMModel::Register) {
        fn.register_count = 3; fn.code.emit(op(vm::Opcode::Add, {0, 1}, 2)); fn.code.emit(ret({2}));
    } else {
        fn.local_count = 2;
        auto get = op(vm::Opcode::LocalGet); get.index = 0; fn.code.emit(get); get.index = 1; fn.code.emit(get);
        fn.code.emit(vm::Opcode::Add); fn.code.emit(ret());
    }
    module.add_function(std::move(fn)); return module;
}
inline void vm_error(const std::string& code, const std::function<void()>& body) {
    bool caught = false;
    try { body(); } catch (const vm::VMError& error) { CHECK(error.code == code); caught = true; }
    CHECK(caught);
}
inline std::int64_t integer_result(const vm::ExecutionResult& result) {
    CHECK(result.stop == vm::ExecutionStop::Returned && result.state.complete());
    return vm::require<std::int64_t>(*result.state.result);
}
