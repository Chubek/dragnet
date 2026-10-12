#pragma once
#include "Module.hpp"
#include "../libir/SimpleSSAGraph.hpp"

namespace firestone::vm {

inline ValueKind runtime_kind(const ir::Type& type) {
    switch (type.kind) {
    case ir::TypeKind::Void: return ValueKind::Null;
    case ir::TypeKind::Integer:
        if (!type.bits || type.bits > 64) throw std::invalid_argument("VM integer width exceeds 64 bits");
        return type.bits == 1 ? ValueKind::Boolean : ValueKind::Integer;
    case ir::TypeKind::Float:
        if (type.bits != 64) throw std::invalid_argument("VM floating values require binary64");
        return ValueKind::Float;
    case ir::TypeKind::Pointer: return ValueKind::Address;
    default: throw std::invalid_argument("IR type has no VM representation");
    }
}
namespace compiler_detail {
class Lowering {
public:
    Lowering(Module& module, const ir::SimpleSSAGraph& ssa, std::string name, VMModel model)
        : module_(module), ssa_(ssa) {
        fn_.name = std::move(name); fn_.model = model;
        auto definitions = ssa_.definitions();
        for (const auto& definition : definitions) if (definition.second.parameter) {
            slots_[definition.first] = next_slot_++; fn_.signature.parameters.push_back(runtime_kind(ssa_.type(definition.first)));
        }
        for (Index v = 0; v < ssa_.value_count(); ++v) {
            runtime_kind(ssa_.type(v));
            if (!slots_.count(v)) slots_[v] = next_slot_++;
        }
        std::optional<ValueKind> result;
        for (auto block : ssa_.cfg().blocks()) for (const auto& i : ssa_.cfg().block(block).instructions) if (i.opcode == ir::Opcode::Return) {
            auto current = i.operands.empty() ? ValueKind::Null : runtime_kind(operand_type(i.operands[0]));
            if (result && *result != current) throw std::invalid_argument("inconsistent SSA return kinds");
            result = current;
        }
        fn_.signature.result = result.value_or(ValueKind::Null);
    }
    Index run() {
        // Declare before lowering so recursive calls can resolve their signature.
        auto id = module_.add_function(fn_);
        auto blocks = ssa_.cfg().blocks();
        auto entry = ssa_.cfg().entry();
        blocks.erase(std::remove(blocks.begin(), blocks.end(), entry), blocks.end()); blocks.insert(blocks.begin(), entry);
        for (auto block : blocks) {
            block_positions_[block] = fn_.code.size();
            for (const auto& i : ssa_.cfg().block(block).instructions) lower(block, i);
        }
        // Phi assignments occur on split edges. Sources are saved before any destination changes.
        for (const auto& edge : edges_) {
            auto position = fn_.code.size(); edge_positions_[edge] = position;
            std::vector<Index> destinations, temporary;
            for (const auto& phi : ssa_.cfg().block(edge.second).instructions) {
                if (phi.opcode != ir::Opcode::Phi) break;
                auto found = std::find(phi.incoming_blocks.begin(), phi.incoming_blocks.end(), edge.first);
                if (found == phi.incoming_blocks.end()) throw std::invalid_argument("missing phi incoming edge");
                auto operand = phi.operands.at(static_cast<Index>(found - phi.incoming_blocks.begin()));
                destinations.push_back(slots_.at(*phi.result));
                auto source = materialize(operand);
                if (fn_.model == VMModel::Register) {
                    auto temp = next_slot_++; Instruction save; save.opcode = Opcode::Move; save.destination = temp; save.operands = {source}; fn_.code.emit(save); temporary.push_back(temp);
                }
            }
            if (fn_.model == VMModel::Register) {
                for (Index n = 0; n < destinations.size(); ++n) {
                    Instruction move; move.opcode = Opcode::Move; move.destination = destinations[n]; move.operands = {temporary[n]}; fn_.code.emit(move);
                }
            } else {
                for (auto it = destinations.rbegin(); it != destinations.rend(); ++it) {
                    Instruction store; store.opcode = Opcode::LocalSet; store.index = *it; fn_.code.emit(store);
                }
            }
            Instruction jump; jump.opcode = Opcode::Jump; jump.target = block_positions_.at(edge.second); fn_.code.emit(jump);
        }
        for (const auto& patch : patches_) {
            auto& i = fn_.code.at(patch.pc); auto target = edge_positions_.at(patch.edge);
            if (patch.alternate) i.alternate = target; else i.target = target;
        }
        if (fn_.model == VMModel::Register) fn_.register_count = next_slot_; else fn_.local_count = next_slot_;
        module_.function(id) = std::move(fn_); return id;
    }
private:
    using Edge = std::pair<ir::BlockId, ir::BlockId>;
    struct Patch { Index pc; Edge edge; bool alternate; };
    ir::Type operand_type(const ir::Operand& operand) const {
        return operand.kind == ir::OperandKind::Value ? ssa_.type(operand.value) : operand.type;
    }
    Index materialize(const ir::Operand& operand) {
        Instruction i;
        if (operand.kind == ir::OperandKind::Value) {
            auto slot = slots_.at(operand.value);
            if (fn_.model == VMModel::Register) return slot;
            i.opcode = Opcode::LocalGet; i.index = slot; fn_.code.emit(i); return no_index;
        }
        if (operand.kind == ir::OperandKind::Symbol) { i.opcode = Opcode::GetGlobal; i.symbol = operand.symbol; }
        else {
            i.opcode = Opcode::Constant;
            Value value;
            if (operand.kind == ir::OperandKind::Float) { runtime_kind(operand.type); value = operand.floating; }
            else {
                auto tag = runtime_kind(operand.type);
                if (tag == ValueKind::Boolean) {
                    if (operand.integer != 0 && operand.integer != 1) throw std::invalid_argument("invalid boolean literal");
                    value = operand.integer != 0;
                } else {
                    if (!sema::SignedRange(operand.type.bits).contains(operand.integer)) throw std::invalid_argument("integer literal exceeds signed width");
                    value = operand.integer;
                }
            }
            i.index = module_.constants.add(std::move(value));
        }
        if (fn_.model == VMModel::Register) i.destination = next_slot_++;
        auto result = i.destination; fn_.code.emit(std::move(i)); return result;
    }
    void lower(ir::BlockId block, const ir::Instruction& source) {
        if (source.opcode == ir::Opcode::Phi) return;
        if (source.opcode == ir::Opcode::Constant || source.opcode == ir::Opcode::Copy) {
            auto input = materialize(source.operands.at(0));
            Instruction copy; copy.opcode = Opcode::Move;
            if (fn_.model == VMModel::Register) { copy.destination = slots_.at(*source.result); copy.operands = {input}; }
            fn_.code.emit(copy); save_stack_result(source); return;
        }
        Instruction i;
        if (source.opcode >= ir::Opcode::Add && source.opcode <= ir::Opcode::Ge) {
            i.opcode = static_cast<Opcode>(static_cast<int>(Opcode::Add) + static_cast<int>(source.opcode) - static_cast<int>(ir::Opcode::Add));
            auto type = operand_type(source.operands.at(0));
            if (!type.numeric()) throw std::invalid_argument("VM arithmetic requires numeric operands");
            i.bits = type.bits;
            if (type == ir::Type::boolean() && i.opcode != Opcode::And && i.opcode != Opcode::Or && i.opcode != Opcode::Xor && i.opcode != Opcode::Not && !ir::is_comparison(source.opcode))
                throw std::invalid_argument("unsupported boolean arithmetic");
        } else switch (source.opcode) {
        case ir::Opcode::Select: i.opcode = Opcode::Select; break;
        case ir::Opcode::Load: i.opcode = Opcode::Load; break;
        case ir::Opcode::Store: i.opcode = Opcode::Store; break;
        case ir::Opcode::Call: {
            i.argument_count = source.operands.size();
            if (module_.natives.count(source.annotation)) { i.opcode = Opcode::NativeCall; i.symbol = source.annotation; }
            else { i.opcode = Opcode::Call; i.index = module_.find(source.annotation); }
            const auto& signature = callee_signature(module_, i);
            if (signature.parameters.size() != source.operands.size() || signature.result != runtime_kind(source.type))
                throw std::invalid_argument("SSA call signature mismatch");
            for (Index n = 0; n < source.operands.size(); ++n)
                if (signature.parameters[n] != runtime_kind(operand_type(source.operands[n]))) throw std::invalid_argument("SSA call argument kind mismatch");
            break;
        }
        case ir::Opcode::Jump: i.opcode = Opcode::Jump; break;
        case ir::Opcode::Branch: i.opcode = Opcode::Branch; break;
        case ir::Opcode::Return: i.opcode = Opcode::Return; i.argument_count = source.operands.size(); break;
        default: throw std::invalid_argument("unsupported SSA opcode");
        }
        for (const auto& operand : source.operands) {
            auto value = materialize(operand); if (fn_.model == VMModel::Register) i.operands.push_back(value);
        }
        if (source.result && fn_.model == VMModel::Register) i.destination = slots_.at(*source.result);
        auto pc = fn_.code.emit(i); save_stack_result(source);
        for (Index n = 0; n < source.targets.size(); ++n) {
            Edge edge{block, source.targets[n]}; edges_.insert(edge); patches_.push_back({pc, edge, n != 0});
        }
    }
    void save_stack_result(const ir::Instruction& source) {
        if (source.result && fn_.model == VMModel::Stack) {
            Instruction store; store.opcode = Opcode::LocalSet; store.index = slots_.at(*source.result); fn_.code.emit(store);
        }
    }
    Module& module_;
    const ir::SimpleSSAGraph& ssa_;
    Function fn_;
    Index next_slot_ = 0;
    std::map<ir::ValueId, Index> slots_;
    std::map<ir::BlockId, Index> block_positions_;
    std::set<Edge> edges_;
    std::map<Edge, Index> edge_positions_;
    std::vector<Patch> patches_;
};
} // namespace compiler_detail

// Transactional: failures preserve the original module, including its constants.
inline Index compile(Module& module, const ir::SimpleSSAGraph& ssa, std::string name, VMModel model = VMModel::Register) {
    if (model != VMModel::Register && model != VMModel::Stack) throw std::invalid_argument("invalid compilation model");
    auto errors = ssa.verify();
    if (!errors.empty() || ssa.cfg().blocks().empty()) throw std::invalid_argument("cannot compile invalid or empty SSA");
    Module candidate = module;
    auto id = compiler_detail::Lowering(candidate, ssa, std::move(name), model).run();
    require_valid(candidate); module = std::move(candidate); return id;
}
class Compiler {
public:
    Index compile(Module& module, const ir::SimpleSSAGraph& ssa, std::string name, VMModel model = VMModel::Register) const {
        return vm::compile(module, ssa, std::move(name), model);
    }
};

} // namespace firestone::vm
