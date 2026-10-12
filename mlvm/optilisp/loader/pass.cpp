#include <mlvm/optilisp/pass.hpp>

namespace mlvm::optilisp {
Pass kernel_pass(Kernel kernel, std::shared_ptr<ModuleOptimizer> optimizer) {
    Pass pass;
    pass.name = kernel.id;
    pass.requires_analyses = kernel.analyses.requires_analyses;
    pass.preserves_analyses = kernel.analyses.preserves;
    pass.run = [kernel = std::move(kernel), optimizer = std::move(optimizer)]
        (Module& module, AnalysisManager& analyses, DiagnosticEngine& diagnostics) {
        if (!optimizer || !optimizer->supports(kernel)) {
            diagnostics.error("optilisp.backend", "no optimizer implementation for " + kernel.id);
            return false;
        }
        if (optimizer->stage(module) != kernel.stage) return true;
        auto positive = [&](const std::string& key) { return optimizer->fact(key, kernel, module, analyses) == Fact::proven_true; };
        auto negative = [&](const std::string& key) { return optimizer->fact(key, kernel, module, analyses) == Fact::proven_false; };
        for (const auto& requirement : kernel.requirements)
            if (!positive(requirement.kind + ":" + requirement.id)) return true;
        for (const auto& precondition : kernel.preconditions) if (!positive("condition:" + precondition)) return true;
        for (const auto& forbidden : kernel.forbids) if (!negative("condition:" + forbidden)) return true;
        for (const auto& prior : kernel.prerequisites) if (!positive("history:" + prior)) return true;
        for (const auto& prior : kernel.forbidden_prior) if (!negative("history:" + prior)) return true;
        return optimizer->apply(kernel, module, analyses, diagnostics);
    };
    return pass;
}
} // namespace mlvm::optilisp
