#pragma once
#include "Common.hpp"

namespace firestone::ir {

using RegionId = std::size_t;
enum class RegionalNodeKind { Simple, Gamma, Theta, Lambda };
struct OutputPort {
    RegionId region = invalid_id;
    NodeId node = invalid_id; // invalid_id denotes a region argument.
    std::size_t index = 0;
};
struct RegionalNode {
    RegionalNodeKind kind = RegionalNodeKind::Simple;
    RegionId region = 0;
    Opcode opcode = Opcode::Copy;
    std::vector<OutputPort> inputs;
    std::vector<Type> outputs;
    std::vector<RegionId> subregions;
    std::vector<Type> return_types; // Lambda signature.
    std::optional<Operand> literal;
};
struct Region {
    std::optional<NodeId> owner;
    std::vector<Type> arguments;
    std::vector<OutputPort> results;
    std::vector<NodeId> nodes;
};

// Regional value/state dependence graph. State is an explicit Type::memory()
// port; no hidden effect ordering is inferred across a region boundary.
class RVSDG {
public:
    RVSDG() { regions_.push_back({}); }
    RegionId root() const { return 0; }
    const Region& region(RegionId id) const { return regions_.at(id); }
    const RegionalNode& node(NodeId id) const { return nodes_.at(id); }
    OutputPort argument(RegionId region, Type type) {
        auto& r = regions_.at(region); auto index = r.arguments.size(); r.arguments.push_back(std::move(type)); return {region, invalid_id, index};
    }
    OutputPort argument_port(RegionId region, std::size_t index) const { (void)regions_.at(region).arguments.at(index); return {region, invalid_id, index}; }
    OutputPort output(NodeId node, std::size_t index = 0) const { const auto& n = nodes_.at(node); (void)n.outputs.at(index); return {n.region, node, index}; }
    Type type(const OutputPort& port) const {
        (void)regions_.at(port.region);
        if (port.node == invalid_id) return regions_.at(port.region).arguments.at(port.index);
        const auto& n = nodes_.at(port.node);
        if (n.region != port.region) throw std::invalid_argument("port region differs from producer region");
        return n.outputs.at(port.index);
    }
    NodeId simple(RegionId region, Opcode opcode, std::vector<OutputPort> inputs, std::vector<Type> outputs) {
        if (is_terminator(opcode) || opcode == Opcode::Phi) throw std::invalid_argument("regional control uses structural nodes");
        if (opcode == Opcode::Constant) throw std::invalid_argument("use constant for regional literals");
        return add_node({RegionalNodeKind::Simple, region, opcode, std::move(inputs), std::move(outputs), {}, {}, {}});
    }
    NodeId constant(RegionId region, Operand literal) {
        if (literal.kind == OperandKind::Value) throw std::invalid_argument("regional constant requires an immediate");
        Instruction i{Opcode::Constant, 0, literal.type, {literal}, {}, {}, {}};
        auto errors = verify_instruction(i, [](ValueId) { return Type{}; });
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        return add_node({RegionalNodeKind::Simple, region, Opcode::Constant, {}, {literal.type}, {}, {}, std::move(literal)});
    }
    NodeId gamma(RegionId region, OutputPort predicate, std::vector<OutputPort> inputs, std::vector<Type> outputs) {
        if (type(predicate) != Type::boolean()) throw std::invalid_argument("gamma predicate must be boolean");
        auto args = port_types(inputs); inputs.insert(inputs.begin(), predicate);
        auto id = add_node({RegionalNodeKind::Gamma, region, Opcode::Select, std::move(inputs), std::move(outputs), {}, {}, {}});
        try { add_subregion(id, args); add_subregion(id, args); }
        catch (...) { regions_.pop_back(); nodes_.pop_back(); regions_.at(region).nodes.pop_back(); throw; }
        return id;
    }
    NodeId theta(RegionId region, std::vector<OutputPort> initial) {
        auto types = port_types(initial);
        auto id = add_node({RegionalNodeKind::Theta, region, Opcode::Phi, std::move(initial), types, {}, {}, {}});
        add_subregion(id, std::move(types)); return id;
    }
    NodeId lambda(RegionId region, std::vector<Type> arguments, std::vector<Type> results) {
        auto id = add_node({RegionalNodeKind::Lambda, region, Opcode::Call, {}, {Type::function()}, {}, std::move(results), {}});
        add_subregion(id, std::move(arguments)); return id;
    }
    void set_results(RegionId region, std::vector<OutputPort> results) {
        check_inputs(region, results); regions_.at(region).results = std::move(results);
    }
    Diagnostics verify() const {
        Diagnostics out;
        for (NodeId id = 0; id < nodes_.size(); ++id) {
            const auto& n = nodes_.at(id);
            for (const auto& t : n.outputs) if (!t.valid_value()) diagnose(out, "type", "regional output has no valid value type", id);
            for (auto r : n.subregions) {
                if (r >= regions_.size()) { diagnose(out, "region", "subregion id is out of range", id); continue; }
                const auto actual = port_types(regions_.at(r).results);
                std::vector<Type> expected = n.outputs;
                if (n.kind == RegionalNodeKind::Theta) expected.insert(expected.begin(), Type::boolean());
                if (n.kind == RegionalNodeKind::Lambda) expected = n.return_types;
                if (actual != expected) diagnose(out, "region", "subregion result signature differs", id);
                auto args = port_types(n.inputs);
                if (n.kind == RegionalNodeKind::Gamma) args.erase(args.begin());
                if (n.kind != RegionalNodeKind::Lambda && regions_[r].arguments != args) diagnose(out, "region", "subregion argument signature differs", id);
            }
            if (n.kind == RegionalNodeKind::Simple) {
                // Pure scalar operations follow the shared instruction contract.
                // Explicit stateful nodes may have multiple outputs and are
                // checked using the value/state signature below.
                if (!is_effectful(n.opcode)) {
                    if (n.outputs.size() != 1) { diagnose(out, "arity", "scalar node requires one output", id); continue; }
                    if (n.opcode == Opcode::Constant) {
                        if (!n.inputs.empty() || !n.literal) diagnose(out, "literal", "regional constant requires a literal and no inputs", id);
                        else append_diagnostics(out, verify_instruction({Opcode::Constant, id, n.outputs[0], {*n.literal}, {}, {}, {}}, [](ValueId) { return Type{}; }), id, 0);
                        continue;
                    }
                    Instruction i; i.opcode = n.opcode; i.result = id; i.type = n.outputs[0];
                    for (const auto& p : n.inputs) i.operands.push_back(Operand::named("port", type(p)));
                    append_diagnostics(out, verify_instruction(i, [](ValueId) { return Type{}; }), id, 0);
                } else {
                    const auto inputs = port_types(n.inputs);
                    if (inputs.empty() || inputs.back() != Type::memory() || n.outputs.empty() || n.outputs.back() != Type::memory())
                        diagnose(out, "state", "effectful node requires a final memory input and output", id);
                    if (n.opcode == Opcode::Load && (inputs.size() != 2 || inputs[0].kind != TypeKind::Pointer || n.outputs.size() != 2))
                        diagnose(out, "state", "load signature is (pointer, memory) -> (value, memory)", id);
                    if (n.opcode == Opcode::Store && (inputs.size() != 3 || inputs[0].kind != TypeKind::Pointer || n.outputs.size() != 1))
                        diagnose(out, "state", "store signature is (pointer, value, memory) -> memory", id);
                }
            }
        }
        for (RegionId r = 0; r < regions_.size(); ++r) for (const auto& t : regions_.at(r).arguments)
            if (!t.valid_value()) diagnose(out, "type", "region argument has no valid value type", r);
        return out;
    }
private:
    std::vector<Region> regions_;
    std::vector<RegionalNode> nodes_;
    std::vector<Type> port_types(const std::vector<OutputPort>& ports) const {
        std::vector<Type> types; for (const auto& p : ports) types.push_back(type(p)); return types;
    }
    void check_inputs(RegionId region, const std::vector<OutputPort>& inputs) const {
        (void)regions_.at(region);
        for (const auto& p : inputs) { type(p); if (p.region != region) throw std::invalid_argument("input crosses a region boundary; use an argument port"); }
    }
    NodeId add_node(RegionalNode node) {
        check_inputs(node.region, node.inputs);
        auto id = nodes_.size(); regions_.at(node.region).nodes.push_back(id); nodes_.push_back(std::move(node)); return id;
    }
    void add_subregion(NodeId owner, std::vector<Type> arguments) {
        auto id = regions_.size(); regions_.push_back({owner, std::move(arguments), {}, {}}); nodes_.at(owner).subregions.push_back(id);
    }
};

} // namespace firestone::ir
