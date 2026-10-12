#pragma once
#include "OSR.hpp"

namespace firestone::vm {

struct DeoptResult {
    ExecutionState state;
    bool optimized = false;
    std::string reason;
};
inline ExecutionState deoptimize(const Module& module, const ExecutionState& state, const OSRPlan& reconstruction,
                                 const ExecutionLimits& limits = {}) {
    return transfer_osr(module, state, reconstruction, limits);
}

} // namespace firestone::vm
