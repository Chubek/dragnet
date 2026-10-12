#pragma once
#include "ThreadedCode.hpp"

namespace firestone::vm {

// Bind instructions into zero-decode host subroutines. Branching remains explicit.
class SubroutineThreadedCode {
public:
    SubroutineThreadedCode(const CodeTape& code, const HandlerTable& handlers) {
        for (const auto& i : code.instructions()) {
            auto handler = handler_at(handlers, i.opcode);
            subroutines_.push_back([i, handler](Index pc) { return handler(i, pc); });
        }
    }
    ThreadedResult run(Index start = 0, Index budget = 1000000) const {
        return run_threaded(subroutines_.size(), [&](Index pc) { return subroutines_.at(pc)(pc); }, start, budget);
    }
private:
    std::vector<std::function<Index(Index)>> subroutines_;
};

} // namespace firestone::vm
