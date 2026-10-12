#pragma once
#include "AOT.hpp"
#include "Deopt.hpp"

namespace firestone::vm {

struct Trace {
    std::vector<CodeAddress> path;
    std::vector<Guard> guards;
    bool truncated = false;
};
struct TraceCapture { ExecutionResult execution; Trace trace; };
using TraceBody = std::function<DeoptResult(Environment&, const ExecutionState&, const ExecutionLimits&)>;
using TraceCompiler = std::function<TraceBody(const Module&, const Trace&)>;

class TraceJIT {
public:
    explicit TraceJIT(Module module) : module_(std::make_shared<const Module>(std::move(module))) { require_valid(*module_); }
    TraceCapture record(Index function, const std::vector<Value>& arguments, Environment& environment,
                        Index trace_limit = 4096, const ExecutionLimits& limits = {}) {
        Trace trace;
        auto result = execute(*module_, function, arguments, environment, limits, [&](CodeAddress pc, const Instruction&) {
            auto key = std::make_pair(pc.function, pc.instruction);
            if (!heat_.count(key) && heat_.size() >= 65536) heat_.erase(heat_.begin());
            ++heat_[key];
            if (trace.path.size() < trace_limit) trace.path.push_back(pc); else trace.truncated = true;
        });
        return {std::move(result), std::move(trace)};
    }
    Index heat(CodeAddress position) const {
        auto found = heat_.find({position.function, position.instruction}); return found == heat_.end() ? 0 : found->second;
    }
    Index compile(Trace trace, const TraceCompiler& compiler) {
        if (trace.path.empty() || !compiler) throw std::invalid_argument("empty trace or backend");
        for (auto pc : trace.path)
            if (pc.function >= module_->size() || pc.instruction >= module_->function(pc.function).code.size()) throw std::invalid_argument("invalid trace position");
        auto body = compiler(*module_, trace);
        if (!body) throw std::invalid_argument("backend declined trace");
        traces_.push_back({std::move(trace), std::move(body)}); return traces_.size() - 1;
    }
    DeoptResult enter(Index trace, Environment& environment, const ExecutionState& state, const ExecutionLimits& limits = {}) const {
        validate_state(*module_, state, limits); const auto& entry = traces_.at(trace);
        if (state.complete()) return {state, false, "completed"};
        CodeAddress pc{state.frames.top().function, state.frames.top().pc};
        if (!(pc == entry.trace.path.front())) return {state, false, "entry-position"};
        if (!check_guards(state, entry.trace.guards)) return {state, false, "guard"};
        auto result = entry.body(environment, state, limits); validate_state(*module_, result.state, limits);
        if (result.state.instructions < state.instructions || result.state.instructions - state.instructions > limits.instructions)
            throw VMError("vm.backend", "trace instruction accounting mismatch");
        if (result.state.complete()) check_result(module_->function(state.frames.at(0).function).signature, *result.state.result);
        return result;
    }
    const Module& module() const { return *module_; }
private:
    struct Entry { Trace trace; TraceBody body; };
    std::shared_ptr<const Module> module_;
    std::map<std::pair<Index, Index>, Index> heat_;
    std::vector<Entry> traces_;
};

} // namespace firestone::vm
