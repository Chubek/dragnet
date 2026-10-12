#pragma once
#include "CodeTape.hpp"
#include "ConstantPool.hpp"
#include <deque>
#include <iterator>
#include <set>

namespace firestone::vm {

struct Function {
    std::string name;
    VMModel model = VMModel::Register;
    Signature signature;
    Index register_count = 0, local_count = 0;
    CodeTape code;
};
class Module {
public:
    ConstantPool constants;
    std::map<std::string, Signature> natives;
    Index add_function(Function function) {
        if (function.name.empty()) throw std::invalid_argument("empty function name");
        for (const auto& existing : functions_) if (existing.name == function.name) throw std::invalid_argument("duplicate function name");
        functions_.push_back(std::move(function)); return functions_.size() - 1;
    }
    Index find(const std::string& name) const {
        for (Index i = 0; i < functions_.size(); ++i) if (functions_[i].name == name) return i;
        throw std::out_of_range("unknown bytecode function: " + name);
    }
    Function& function(Index index) { return functions_.at(index); }
    const Function& function(Index index) const { return functions_.at(index); }
    Index size() const { return functions_.size(); }
    ir::Diagnostics verify(Index evaluation_limit = 1000000) const;
private:
    std::vector<Function> functions_;
};
inline const Signature& callee_signature(const Module& module, const Instruction& instruction) {
    if (instruction.opcode == Opcode::Call) return module.function(instruction.index).signature;
    auto found = module.natives.find(instruction.symbol);
    if (found == module.natives.end()) throw std::out_of_range("undeclared native symbol");
    return found->second;
}
inline Index input_count(const Instruction& instruction) {
    auto count = opcode_info(instruction.opcode).inputs;
    return count < 0 ? instruction.argument_count : static_cast<Index>(count);
}
inline Index output_count(const Module& module, const Instruction& instruction) {
    auto count = opcode_info(instruction.opcode).outputs;
    return count < 0 ? (callee_signature(module, instruction).result != ValueKind::Null ? 1 : 0) : static_cast<Index>(count);
}
inline std::vector<Index> successors(const Function& function, Index pc) {
    const auto& i = function.code.at(pc);
    if (i.opcode == Opcode::Return) return {};
    if (i.opcode == Opcode::Jump) return {i.target};
    if (i.opcode == Opcode::Branch) return {i.target, i.alternate};
    return {pc + 1};
}
inline ir::Diagnostics Module::verify(Index evaluation_limit) const {
    ir::Diagnostics errors;
    std::set<std::string> names;
    auto valid_signature = [](const Signature& s) {
        return valid_kind(s.result) && std::all_of(s.parameters.begin(), s.parameters.end(), valid_kind);
    };
    for (const auto& native : natives)
        if (native.first.empty() || !valid_signature(native.second)) ir::diagnose(errors, "vm.signature", "invalid native declaration");
    for (Index f = 0; f < size(); ++f) {
        const auto& fn = function(f);
        const auto before = errors.size();
        auto error = [&](const char* code, const char* message, Index pc = no_index) { ir::diagnose(errors, code, message, f, pc); };
        if (fn.name.empty() || !names.insert(fn.name).second) error("vm.name", "empty or duplicate function name");
        if (fn.model != VMModel::Stack && fn.model != VMModel::Register) error("vm.model", "unknown VM model");
        if (!valid_signature(fn.signature)) error("vm.signature", "invalid function signature");
        const auto slots = fn.model == VMModel::Stack ? fn.local_count : fn.register_count;
        if (slots < fn.signature.parameters.size()) error("vm.parameters", "insufficient parameter slots");
        if (fn.register_count > evaluation_limit || fn.local_count > evaluation_limit || fn.code.size() > evaluation_limit)
            error("vm.verify.limit", "function exceeds verifier resource budget");
        if (!fn.code.size()) error("vm.code", "function has no code");
        for (Index pc = 0; pc < fn.code.size(); ++pc) {
            const auto& i = fn.code.at(pc);
            if (!valid_opcode(i.opcode)) { error("vm.opcode", "unknown opcode", pc); continue; }
            if (!i.bits || i.bits > 64) error("vm.width", "invalid scalar width", pc);
            if (i.opcode == Opcode::Constant && i.index >= constants.size()) error("vm.constant", "unknown constant", pc);
            if ((i.opcode == Opcode::LocalGet || i.opcode == Opcode::LocalSet) && i.index >= fn.local_count)
                error("vm.local", "unknown local slot", pc);
            const bool call = i.opcode == Opcode::Call || i.opcode == Opcode::NativeCall;
            bool valid_call = true;
            if (i.opcode == Opcode::Call && i.index >= size()) { error("vm.call", "unknown callee", pc); valid_call = false; }
            if (i.opcode == Opcode::NativeCall && !natives.count(i.symbol)) { error("vm.native", "undeclared native symbol", pc); valid_call = false; }
            if (call && valid_call && i.argument_count != callee_signature(*this, i).parameters.size())
                error("vm.arity", "call arity differs from declaration", pc);
            if (i.opcode == Opcode::Return && i.argument_count != (fn.signature.result == ValueKind::Null ? 0u : 1u))
                error("vm.return", "return arity differs from signature", pc);
            if (!call && i.opcode != Opcode::Return && i.argument_count) error("vm.arity", "unexpected argument count", pc);
            if ((i.opcode == Opcode::GetGlobal || i.opcode == Opcode::SetGlobal || i.opcode == Opcode::GetField || i.opcode == Opcode::SetField) && i.symbol.empty())
                error("vm.symbol", "empty runtime symbol", pc);
            if (i.opcode != Opcode::Jump && i.opcode != Opcode::Branch && i.target != no_index)
                error("vm.target", "unexpected branch target", pc);
            if (i.opcode != Opcode::Branch && i.alternate != no_index) error("vm.target", "unexpected alternate target", pc);
            for (auto next : successors(fn, pc)) if (next >= fn.code.size()) error("vm.target", "control falls outside code", pc);
            if (fn.model == VMModel::Register) {
                if (i.opcode == Opcode::LocalGet || i.opcode == Opcode::LocalSet || i.opcode == Opcode::Pop || i.opcode == Opcode::Dup || i.opcode == Opcode::Swap)
                    error("vm.model", "stack-only opcode in register function", pc);
                if (i.operands.size() != input_count(i)) error("vm.arity", "register operand count", pc);
                for (auto reg : i.operands) if (reg >= fn.register_count) error("vm.register", "unknown operand register", pc);
                if (valid_call) {
                    auto outputs = output_count(*this, i);
                    if ((outputs != 0) != (i.destination != no_index)) error("vm.result", "invalid destination presence", pc);
                    if (outputs && i.destination >= fn.register_count) error("vm.register", "unknown destination register", pc);
                }
            } else if (!i.operands.empty() || i.destination != no_index) error("vm.model", "register fields in stack function", pc);
        }
        if (errors.size() != before) continue;
        struct State { Index depth = 0; std::set<Index> assigned; };
        std::vector<std::optional<State>> states(fn.code.size());
        State initial;
        for (Index i = 0; i < fn.signature.parameters.size(); ++i) initial.assigned.insert(i);
        states[0] = std::move(initial);
        std::deque<Index> pending{0}; std::set<Index> queued{0};
        Index evaluations = 0;
        while (!pending.empty()) {
            auto pc = pending.front(); pending.pop_front(); queued.erase(pc);
            if (evaluations++ >= evaluation_limit) { error("vm.verify.limit", "dataflow budget exhausted", pc); break; }
            auto state = *states[pc]; const auto& i = fn.code.at(pc);
            if (fn.model == VMModel::Stack) {
                auto inputs = input_count(i);
                if (state.depth < inputs) { error("vm.stack.underflow", "operand stack underflow", pc); continue; }
                if (i.opcode == Opcode::Return && state.depth != inputs) error("vm.stack.return", "return leaves extra operands", pc);
                state.depth = state.depth - inputs + output_count(*this, i);
                if (state.depth > evaluation_limit) { error("vm.verify.limit", "operand depth exceeds budget", pc); continue; }
                if (i.opcode == Opcode::LocalSet) state.assigned.insert(i.index);
            } else if (i.destination != no_index) state.assigned.insert(i.destination);
            for (auto next : successors(fn, pc)) {
                bool changed = !states[next];
                if (!states[next]) states[next] = state;
                else {
                    auto& previous = *states[next];
                    if (previous.depth != state.depth) { error("vm.stack.join", "unequal stack heights at join", next); continue; }
                    std::set<Index> intersection;
                    std::set_intersection(previous.assigned.begin(), previous.assigned.end(), state.assigned.begin(), state.assigned.end(),
                        std::inserter(intersection, intersection.end()));
                    changed = intersection != previous.assigned;
                    previous.assigned = std::move(intersection);
                }
                if (changed && queued.insert(next).second) pending.push_back(next);
            }
        }
        for (Index pc = 0; pc < fn.code.size(); ++pc) if (states[pc]) {
            const auto& i = fn.code.at(pc); const auto& assigned = states[pc]->assigned;
            if (fn.model == VMModel::Stack && i.opcode == Opcode::LocalGet && !assigned.count(i.index))
                error("vm.uninitialized", "local is not definitely assigned", pc);
            for (auto operand : i.operands) if (!assigned.count(operand)) error("vm.uninitialized", "register is not definitely assigned", pc);
        }
    }
    return errors;
}
inline void require_valid(const Module& module) {
    auto errors = module.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().code + ": " + errors.front().message);
}

} // namespace firestone::vm
