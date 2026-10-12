#pragma once
#include "Dispatch.hpp"

namespace firestone::vm {

class StackVM {
public:
    ExecutionResult run(const Module& module, Index function, const std::vector<Value>& arguments,
                        Environment& environment, const ExecutionLimits& limits = {}, const Observer& observer = {}) const {
        if (module.function(function).model != VMModel::Stack) throw std::invalid_argument("stack entry function required");
        return execute(module, function, arguments, environment, limits, observer);
    }
};

} // namespace firestone::vm
