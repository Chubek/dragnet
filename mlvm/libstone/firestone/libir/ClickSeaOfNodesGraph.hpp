#pragma once
#include "Graph.hpp"

namespace firestone::ir {

enum class SeaNodeKind { Start, Operation, Region, Phi, Branch, ControlProjection, End };
struct SeaNode {
    SeaNodeKind kind = SeaNodeKind::Start;
    Instruction instruction;
    std::vector<NodeId> values;
    std::vector<NodeId> controls;
    std::size_t projection = 0;
};

// Explicit control and value inputs; loop regions and loop phis can be patched
// after creation using set_inputs, allowing genuine cyclic sea-of-nodes graphs.
class ClickSeaOfNodesGraph {
public:
    ClickSeaOfNodesGraph() { start_ = graph_.add_node({}); }
    NodeId start() const { return start_; }
    NodeId operation(Opcode opcode, Type type, std::vector<NodeId> values = {}, std::optional<NodeId> control = {}) {
        if (is_terminator(opcode) || opcode == Opcode::Phi) throw std::invalid_argument("use region, phi or end for structural nodes");
        SeaNode node; node.kind = SeaNodeKind::Operation; node.instruction.opcode = opcode; node.instruction.type = std::move(type);
        auto id = graph_.add_node(std::move(node));
        try { set_inputs(id, std::move(values), control ? std::vector<NodeId>{*control} : std::vector<NodeId>{}); }
        catch (...) { graph_.remove_node(id); throw; }
        return id;
    }
    NodeId literal(Operand value) {
        if (value.kind == OperandKind::Value) throw std::invalid_argument("literal requires an immediate");
        SeaNode n; n.kind = SeaNodeKind::Operation; n.instruction = {Opcode::Constant, {}, value.type, {std::move(value)}, {}, {}, {}};
        auto id = graph_.add_node(std::move(n)); graph_.node(id).instruction.result = id; return id;
    }
    NodeId region(std::vector<NodeId> controls) {
        SeaNode n; n.kind = SeaNodeKind::Region; auto id = graph_.add_node(std::move(n));
        try { set_inputs(id, {}, std::move(controls)); } catch (...) { graph_.remove_node(id); throw; } return id;
    }
    NodeId phi(NodeId region, Type type, std::vector<NodeId> values) {
        SeaNode n; n.kind = SeaNodeKind::Phi; n.instruction.opcode = Opcode::Phi; n.instruction.type = std::move(type);
        auto id = graph_.add_node(std::move(n));
        try { set_inputs(id, std::move(values), {region}); } catch (...) { graph_.remove_node(id); throw; } return id;
    }
    NodeId end(NodeId control, std::optional<NodeId> value = {}) {
        SeaNode n; n.kind = SeaNodeKind::End; auto id = graph_.add_node(std::move(n));
        try { set_inputs(id, value ? std::vector<NodeId>{*value} : std::vector<NodeId>{}, {control}); }
        catch (...) { graph_.remove_node(id); throw; } return id;
    }
    NodeId branch(NodeId predicate, NodeId control) {
        SeaNode n; n.kind = SeaNodeKind::Branch;
        auto id = graph_.add_node(std::move(n));
        try { set_inputs(id, {predicate}, {control}); }
        catch (...) { graph_.remove_node(id); throw; }
        return id;
    }
    NodeId control_projection(NodeId branch, std::size_t index) {
        if (graph_.node(branch).kind != SeaNodeKind::Branch || index > 1)
            throw std::invalid_argument("control projection requires branch arm zero or one");
        SeaNode n; n.kind = SeaNodeKind::ControlProjection; n.projection = index;
        auto id = graph_.add_node(std::move(n));
        try { set_inputs(id, {}, {branch}); } catch (...) { graph_.remove_node(id); throw; } return id;
    }
    void set_inputs(NodeId id, std::vector<NodeId> values, std::vector<NodeId> controls) {
        graph_.node(id);
        for (auto v : values) graph_.node(v);
        for (auto c : controls) graph_.node(c);
        const auto edges = graph_.edges();
        for (const auto& e : edges) if (e.to == id) graph_.remove_edge(e);
        auto& n = graph_.node(id); n.values = std::move(values); n.controls = std::move(controls);
        if (n.kind == SeaNodeKind::Operation || n.kind == SeaNodeKind::Phi) {
            n.instruction.result = n.instruction.opcode == Opcode::Store ||
                (n.instruction.opcode == Opcode::Call && n.instruction.type.kind == TypeKind::Void) ? std::optional<ValueId>{} : id;
            n.instruction.operands.clear();
            for (auto v : n.values) n.instruction.operands.push_back(Operand::ref(v));
        }
        for (std::size_t p = 0; p < n.values.size(); ++p) graph_.add_edge(n.values[p], id, EdgeKind::Data, p);
        for (std::size_t p = 0; p < n.controls.size(); ++p) graph_.add_edge(n.controls[p], id, EdgeKind::Control, p);
    }
    const DirectedGraph<SeaNode>& graph() const { return graph_; }
    Diagnostics verify() const {
        Diagnostics out;
        for (auto id : graph_.nodes()) {
            const auto& n = graph_.node(id);
            auto is_control = [&](NodeId c) { const auto& source = graph_.node(c); return source.kind == SeaNodeKind::Start || source.kind == SeaNodeKind::Region ||
                source.kind == SeaNodeKind::ControlProjection || (source.kind == SeaNodeKind::Operation && is_effectful(source.instruction.opcode)); };
            for (auto c : n.controls) if (!is_control(c) && !(n.kind == SeaNodeKind::ControlProjection && graph_.node(c).kind == SeaNodeKind::Branch))
                diagnose(out, "control", "input does not produce control", id);
            for (auto v : n.values) if (!graph_.node(v).instruction.result) diagnose(out, "value", "input does not produce a value", id);
            if (n.kind == SeaNodeKind::Start && (!n.controls.empty() || !n.values.empty())) diagnose(out, "start", "start has inputs", id);
            if (n.kind == SeaNodeKind::Region && (n.controls.empty() || !n.values.empty())) diagnose(out, "region", "region needs control inputs only", id);
            if (n.kind == SeaNodeKind::End && (n.controls.size() != 1 || n.values.size() > 1)) diagnose(out, "end", "invalid end shape", id);
            if (n.kind == SeaNodeKind::Branch) {
                if (n.controls.size() != 1 || n.values.size() != 1) diagnose(out, "branch", "branch requires one predicate and one control input", id);
                else if (graph_.node(n.values[0]).instruction.type != Type::boolean()) diagnose(out, "type", "branch predicate must be boolean", id);
            }
            if (n.kind == SeaNodeKind::ControlProjection && (n.controls.size() != 1 || !n.values.empty() || n.projection > 1 ||
                graph_.node(n.controls.front()).kind != SeaNodeKind::Branch)) diagnose(out, "projection", "invalid control projection", id);
            if (n.kind == SeaNodeKind::Operation) {
                append_diagnostics(out, verify_instruction(n.instruction, [&](ValueId v) { return graph_.node(v).instruction.type; }), id, 0);
                if (is_effectful(n.instruction.opcode) && n.controls.size() != 1) diagnose(out, "control", "effectful operation requires one control input", id);
                if (!is_effectful(n.instruction.opcode) && n.controls.size() > 1) diagnose(out, "control", "pure operation has multiple control inputs", id);
            }
            if (n.kind == SeaNodeKind::Phi) {
                if (n.controls.size() != 1 || graph_.node(n.controls.front()).kind != SeaNodeKind::Region)
                    diagnose(out, "phi", "phi requires a region", id);
                else if (n.values.size() != graph_.node(n.controls.front()).controls.size()) diagnose(out, "phi", "phi value count differs from region predecessor count", id);
                if (n.values.empty()) diagnose(out, "phi", "phi has no values", id);
                for (auto v : n.values) if (graph_.node(v).instruction.type != n.instruction.type) diagnose(out, "type", "phi value type differs", id);
            }
        }
        return out;
    }
private:
    DirectedGraph<SeaNode> graph_;
    NodeId start_;
};

} // namespace firestone::ir
