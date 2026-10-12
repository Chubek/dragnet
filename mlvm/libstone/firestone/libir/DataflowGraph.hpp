#pragma once
#include "RawDAG.hpp"

namespace firestone::ir {

class DataflowGraph {
public:
    NodeId input(std::string name, Type type) {
        auto id = dag_.size();
        Instruction inst{Opcode::Constant, id, type, {Operand::named(std::move(name), type)}, {}, {}, {}};
        return dag_.add_node(std::move(inst));
    }
    NodeId operation(Opcode opcode, Type type, std::vector<NodeId> inputs) {
        if (is_terminator(opcode) || is_effectful(opcode) || opcode == Opcode::Phi)
            throw std::invalid_argument("dataflow operations must be pure and acyclic");
        std::vector<Operand> operands;
        for (auto id : inputs) { dag_.node(id); operands.push_back(Operand::ref(id)); }
        auto id = dag_.size();
        Instruction inst{opcode, id, std::move(type), std::move(operands), {}, {}, {}};
        auto errors = verify_instruction(inst, [&](ValueId v) { return dag_.node(v).type; });
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        id = dag_.add_node(std::move(inst));
        for (std::size_t p = 0; p < inputs.size(); ++p) dag_.add_edge(inputs[p], id, EdgeKind::Data, p);
        return id;
    }
    NodeId constant(std::int64_t value, Type type = Type::integer()) {
        auto id = dag_.size();
        Instruction i{Opcode::Constant, id, type, {Operand::constant(value, type)}, {}, {}, {}};
        auto errors = verify_instruction(i, [](ValueId) { return Type{}; });
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        return dag_.add_node(std::move(i));
    }
    const RawDAG<Instruction>& graph() const { return dag_; }
    const Instruction& node(NodeId id) const { return dag_.node(id); }
    std::vector<NodeId> schedule() const { return dag_.topological_order(); }
    Diagnostics verify() const {
        auto out = dag_.verify();
        for (auto id : dag_.nodes()) append_diagnostics(out, verify_instruction(dag_.node(id), [&](ValueId v) {
            return dag_.contains(v) ? dag_.node(v).type : Type{};
        }), id, 0);
        return out;
    }
private:
    RawDAG<Instruction> dag_;
};

} // namespace firestone::ir
