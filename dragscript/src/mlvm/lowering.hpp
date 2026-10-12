#pragma once
#include "semantic/dsir.hpp"
#include <mlvm/ir.hpp>
#include <memory>

namespace dragscript::detail {
// Owns the Context and SourceManager that the Module borrows from, so all three
// may be shared by immutable artifacts without outliving their interned data.
struct LoweredModule {
    std::shared_ptr<mlvm::Context> context = std::make_shared<mlvm::Context>();
    std::shared_ptr<mlvm::SourceManager> sources = std::make_shared<mlvm::SourceManager>();
    mlvm::Module module{*context};
    mlvm::Identifier file = 0;   // Source registered in sources, for provenance.
    std::string text;            // Canonical debug text; DSIR stays authoritative.
};

bool register_dialect(mlvm::Context& context, mlvm::DiagnosticEngine& diagnostics);
std::optional<LoweredModule> lower(const Program& program, mlvm::DiagnosticEngine& diagnostics);
// Immutable compilation output; shared by value across Engine instances.
struct Artifact {
    Program program;                        // Authoritative semantics and provenance.
    std::string mlvm_ir;                    // Canonical debug text of the verified module.
    std::shared_ptr<LoweredModule> lowered; // Verified module for the MLVM execution backend.
};
// Executes a verified module with the shared runtime contracts; see mlvm/tree_interpreter.cpp.
RunResult interpret_mlvm(const LoweredModule& module, const Program& program, const Hosts& hosts,
                         const EngineOptions& engine_options, RunOptions options);
} // namespace dragscript::detail