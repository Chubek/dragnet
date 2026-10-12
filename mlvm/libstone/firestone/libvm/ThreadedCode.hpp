#pragma once
#include "CodeTape.hpp"
#include <memory>

namespace firestone::vm {

// Host handlers return the next tape position, or no_index to finish.
using ThreadedHandler = std::function<Index(const Instruction&, Index)>;
using HandlerTable = std::array<ThreadedHandler, opcode_count>;
struct ThreadedResult { Index next = 0, instructions = 0; bool complete = false; };
template<class Dispatch> inline ThreadedResult run_threaded(Index size, Dispatch dispatch, Index start, Index budget) {
    ThreadedResult result{start, 0, start == no_index};
    while (!result.complete && result.instructions < budget) {
        if (result.next >= size) throw VMError("vm.pc", "threaded code position outside tape");
        result.next = dispatch(result.next); ++result.instructions;
        if (result.next != no_index && result.next >= size) throw VMError("vm.pc", "threaded handler returned an invalid position");
        result.complete = result.next == no_index;
    }
    return result;
}
inline const ThreadedHandler& handler_at(const HandlerTable& handlers, Opcode opcode) {
    opcode_info(opcode);
    const auto& handler = handlers.at(static_cast<Index>(opcode));
    if (!handler) throw std::invalid_argument("unbound threaded opcode handler");
    return handler;
}

} // namespace firestone::vm
