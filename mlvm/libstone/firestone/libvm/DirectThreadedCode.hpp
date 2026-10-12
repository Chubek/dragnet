#pragma once
#include "ThreadedCode.hpp"

namespace firestone::vm {

// Each instruction embeds a resolved callable; no opcode-table lookup during dispatch.
class DirectThreadedCode {
public:
    DirectThreadedCode(const CodeTape& code, const HandlerTable& handlers) {
        for (const auto& i : code.instructions()) entries_.push_back({i, handler_at(handlers, i.opcode)});
    }
    ThreadedResult run(Index start = 0, Index budget = 1000000) const {
        return run_threaded(entries_.size(), [&](Index pc) { const auto& e = entries_.at(pc); return e.handler(e.instruction, pc); }, start, budget);
    }
private:
    struct Entry { Instruction instruction; ThreadedHandler handler; };
    std::vector<Entry> entries_;
};

} // namespace firestone::vm
