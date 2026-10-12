#pragma once
#include "EGraph.hpp"

namespace firestone::eqsat {

class CostModel {
public:
    explicit CostModel(double default_cost = 1, double operand_cost = 0) : default_(default_cost), operand_(operand_cost) {
        validate(default_); validate(operand_);
    }
    void set(std::string op, double cost) {
        if (op.empty()) throw std::invalid_argument("cost operator is empty");
        validate(cost); costs_[std::move(op)] = cost;
    }
    double operator()(const ENode& node) const {
        auto found = costs_.find(node.op);
        double cost = (found == costs_.end() ? default_ : found->second) + operand_ * node.children.size();
        validate(cost); return cost;
    }
private:
    double default_, operand_;
    std::map<std::string, double> costs_;
    static void validate(double cost) { if (!std::isfinite(cost) || cost < 0) throw std::invalid_argument("cost must be finite and nonnegative"); }
};

} // namespace firestone::eqsat
