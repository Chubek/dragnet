#pragma once
#include <mlvm/ir.hpp>
#include <any>
#include <chrono>

namespace mlvm {
class AnalysisManager {
public:
    using Producer = std::function<std::any(const Module&)>;
    bool register_analysis(std::string name, Producer producer);
    const std::any* get(std::string_view name, const Module& module);
    void invalidate(const std::set<std::string>& preserved = {});
private:
    std::map<std::string, Producer, std::less<>> producers_;
    std::map<std::string, std::any, std::less<>> cache_;
};
struct Pass {
    std::string name;
    std::set<std::string> consumes, produces, requires_analyses, preserves_analyses;
    std::function<bool(Module&, AnalysisManager&, DiagnosticEngine&)> run;
};
struct PassEvent {
    std::string name;
    bool success;
    std::chrono::nanoseconds elapsed;
    std::string before, after;
};
class PassManager {
public:
    void add(Pass pass) { passes_.push_back(std::move(pass)); }
    // Each pass commits only after successful verification. Earlier successful passes remain committed.
    bool run(Module& module, DiagnosticEngine& diagnostics);
    AnalysisManager analyses;
    bool dump_ir = false;
    std::function<void(const PassEvent&)> instrumentation;
private:
    std::vector<Pass> passes_;
};
Pass constant_fold_pass();
Pass dead_code_elimination_pass();
Pass canonicalize_pass();
} // namespace mlvm
