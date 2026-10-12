#pragma once
#include "BytecodeBinary.hpp"
#include "Dispatch.hpp"
#include <memory>

namespace firestone::vm {

// Native backends return resumable baseline states on budget or side exits.
using CompiledEntry = std::function<ExecutionResult(const std::vector<Value>&, Environment&, const ExecutionLimits&)>;
using EntryCompiler = std::function<CompiledEntry(const Module&, Index)>;
inline ExecutionResult invoke_compiled(const Module& module, Index function, const CompiledEntry& entry,
                                      const std::vector<Value>& arguments, Environment& environment, const ExecutionLimits& limits) {
    check_arguments(module.function(function).signature, arguments);
    auto result = entry(arguments, environment, limits); validate_state(module, result.state, limits);
    if (result.stop != ExecutionStop::Returned && result.stop != ExecutionStop::Budget) throw VMError("vm.backend", "unknown backend stop reason");
    if ((result.stop == ExecutionStop::Returned) != result.state.complete()) throw VMError("vm.backend", "backend stop/state mismatch");
    if (result.state.instructions > limits.instructions) throw VMError("vm.backend", "backend exceeded instruction budget");
    if (result.state.complete()) check_result(module.function(function).signature, *result.state.result);
    else if (result.state.frames.at(0).function != function) throw VMError("vm.backend", "backend changed root activation");
    return result;
}
class AOT {
public:
    explicit AOT(Module module) : module_(std::make_shared<const Module>(std::move(module))) { require_valid(*module_); }
    void compile(const EntryCompiler& compiler) {
        if (!compiler) throw std::invalid_argument("empty AOT backend");
        std::map<Index, CompiledEntry> candidate;
        for (Index f = 0; f < module_->size(); ++f) { auto body = compiler(*module_, f); if (body) candidate.emplace(f, std::move(body)); }
        entries_ = std::move(candidate);
    }
    ExecutionResult run(Index function, const std::vector<Value>& arguments, Environment& environment, const ExecutionLimits& limits = {}) const {
        auto found = entries_.find(function);
        return found == entries_.end() ? execute(*module_, function, arguments, environment, limits) : invoke_compiled(*module_, function, found->second, arguments, environment, limits);
    }
    BytecodeBytes bytecode() const { return encode(*module_); }
    Index compiled_count() const { return entries_.size(); }
    const Module& module() const { return *module_; }
private:
    std::shared_ptr<const Module> module_;
    std::map<Index, CompiledEntry> entries_;
};

} // namespace firestone::vm
