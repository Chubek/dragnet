#pragma once

#include <mlvm/optilisp/loader.hpp>
#include <mlvm/pass.hpp>

namespace mlvm::optilisp {

// MLVM-specific host interface. Implementations supply actual dialect/NodeIR
// matching, candidate proofs, transformation semantics, and target capabilities.
// The PassManager provides copy-on-run rollback and post-pass IR verification.
class ModuleOptimizer {
public:
    virtual ~ModuleOptimizer() = default;
    virtual bool supports(const Kernel&) const = 0;
    virtual Stage stage(const Module&) const = 0;
    virtual Fact fact(std::string_view namespaced_key, const Kernel&, const Module&, AnalysisManager&) const = 0;
    virtual bool apply(const Kernel&, Module&, AnalysisManager&, DiagnosticEngine&) = 0;
};

// The kernel must come from a successful Loader load. Unknown facts mean normal
// inapplicability. Unsupported verbs/adapters or execution failure fail the pass.
Pass kernel_pass(Kernel kernel, std::shared_ptr<ModuleOptimizer> optimizer);

// Generic SSA integer backend: constructor matching, operand forwarding, and
// modular constant folding. Other semantics require a host ModuleOptimizer.
std::shared_ptr<ModuleOptimizer> integer_optimizer();

// Resolve module.kernel_sources and ordered kernel_applications against the
// supplied Blackstone directories. All requested kernels and IR edits commit
// together; successful execution consumes applications (definitions remain).
bool run_kernels(Module&, const std::filesystem::path& registry_directory,
                 const std::filesystem::path& kernel_directory, DiagnosticEngine&);

} // namespace mlvm::optilisp
