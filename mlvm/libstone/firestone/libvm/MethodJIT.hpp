#pragma once
#include "AOT.hpp"

namespace firestone::vm {

struct MethodProfile { Index invocations = 0, compiled_invocations = 0, compilation_attempts = 0; };
class MethodJIT {
public:
    MethodJIT(Module module, EntryCompiler compiler, Index threshold = 16)
        : module_(std::make_shared<const Module>(std::move(module))), compiler_(std::move(compiler)), threshold_(threshold) {
        require_valid(*module_); if (!compiler_) throw std::invalid_argument("empty method backend");
    }
    ExecutionResult run(Index function, const std::vector<Value>& arguments, Environment& environment, const ExecutionLimits& limits = {}) {
        check_arguments(module_->function(function).signature, arguments);
        auto& profile = profiles_[function]; ++profile.invocations;
        if (!entries_.count(function) && profile.invocations >= threshold_) {
            ++profile.compilation_attempts;
            auto compiled = compiler_(*module_, function); if (compiled) entries_.emplace(function, std::move(compiled));
        }
        auto found = entries_.find(function);
        if (found == entries_.end()) return execute(*module_, function, arguments, environment, limits);
        ++profile.compiled_invocations;
        return invoke_compiled(*module_, function, found->second, arguments, environment, limits);
    }
    MethodProfile profile(Index function) const { module_->function(function); auto found = profiles_.find(function); return found == profiles_.end() ? MethodProfile{} : found->second; }
    // Invalidate keeps hotness profiles so a hot function recompiles on next use.
    void invalidate(Index function) { entries_.erase(function); }
    const Module& module() const { return *module_; }
private:
    std::shared_ptr<const Module> module_;
    EntryCompiler compiler_;
    Index threshold_;
    std::map<Index, CompiledEntry> entries_;
    std::map<Index, MethodProfile> profiles_;
};

} // namespace firestone::vm
