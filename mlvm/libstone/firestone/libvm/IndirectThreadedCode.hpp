#pragma once
#include "ThreadedCode.hpp"

namespace firestone::vm {

// Shared handler cells permit rebinding without rebuilding the instruction tape.
class IndirectThreadedCode {
public:
    IndirectThreadedCode(CodeTape code, std::shared_ptr<HandlerTable> handlers) : code_(std::move(code)), handlers_(std::move(handlers)) {
        if (!handlers_) throw std::invalid_argument("null handler table");
        for (const auto& i : code_.instructions()) handler_at(*handlers_, i.opcode);
    }
    ThreadedResult run(Index start = 0, Index budget = 1000000) const {
        return run_threaded(code_.size(), [&](Index pc) { const auto& i = code_.at(pc); return handler_at(*handlers_, i.opcode)(i, pc); }, start, budget);
    }
private:
    CodeTape code_;
    std::shared_ptr<HandlerTable> handlers_;
};

} // namespace firestone::vm
