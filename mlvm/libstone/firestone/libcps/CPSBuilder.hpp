#pragma once
#include "Continuation.hpp"

namespace firestone::cps {

class CPSBuilder {
public:
    ir::NodeId create(std::string name, std::vector<ir::Type> parameters = {}) {
        if (name.empty() || names_.count(name)) throw std::invalid_argument("continuation name must be unique and nonempty");
        for (const auto& type : parameters) if (!type.valid_value()) throw std::invalid_argument("invalid continuation parameter type");
        auto id = graph_.continuation(name, std::move(parameters)); names_[std::move(name)] = id; return id;
    }
    ir::NodeId lookup(const std::string& name) const { return names_.at(name); }
    ir::Operand parameter(ir::NodeId continuation, std::size_t index) const {
        return ir::Operand::ref(graph_.continuation(continuation).parameters.at(index));
    }
    void jump(ir::NodeId source, ir::NodeId target, std::vector<ir::Operand> arguments) {
        require_transfer(source, target, arguments); graph_.jump(source, target, std::move(arguments));
    }
    void branch(ir::NodeId source, ir::Operand condition, ir::NodeId yes, std::vector<ir::Operand> yes_args,
                ir::NodeId no, std::vector<ir::Operand> no_args) {
        require_transfer(source, yes, yes_args); require_transfer(source, no, no_args);
        auto errors = check_local_operand(graph_, source, condition);
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        auto resolve = [&](ir::ValueId value) { return value < graph_.value_count() ? graph_.type(value) : ir::Type{}; };
        if (ir::operand_type(condition, resolve) != ir::Type::boolean()) throw std::invalid_argument("CPS condition must be boolean");
        graph_.branch(source, std::move(condition), yes, std::move(yes_args), no, std::move(no_args));
    }
    CPSGraph& graph() { return graph_; }
    const CPSGraph& graph() const { return graph_; }
    CPSGraph finish() const {
        auto errors = graph_.verify();
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        return graph_;
    }
private:
    CPSGraph graph_;
    std::map<std::string, ir::NodeId> names_;
    void require_transfer(ir::NodeId source, ir::NodeId target, const std::vector<ir::Operand>& arguments) const {
        auto errors = check_transfer(graph_, source, target, arguments);
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    }
};

} // namespace firestone::cps
