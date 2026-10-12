#pragma once
#include <mlvm/ir.hpp>
#include <filesystem>

namespace cephyr {
struct PreprocessOptions {
    std::vector<std::filesystem::path> include_paths;
    std::map<std::string, std::string> defines;
    std::set<std::string> undefines;
    std::vector<std::filesystem::path> forced_includes;
};
// C17 freestanding subset. Preprocessed sources are registered separately for diagnostics.
std::optional<std::string> preprocess(mlvm::SourceManager& sources, mlvm::Identifier file,
                                     const PreprocessOptions& options, mlvm::DiagnosticEngine& diagnostics);
bool parse(mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics);
// Semantic/lowering subset: int f(void) { return <constant integer expression>; }.
std::optional<mlvm::Module> lower(mlvm::Context& context, mlvm::SourceManager& sources,
                                mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics);
std::optional<std::string> emit_assembly(const mlvm::Module& module, mlvm::DiagnosticEngine& diagnostics);
} // namespace cephyr
