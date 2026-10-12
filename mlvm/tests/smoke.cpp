#include <mlvm/mlvm.hpp>
int main() {
    mlvm::DiagnosticEngine diagnostics;
    diagnostics.report({mlvm::Severity::error, "TEST", "expected test diagnostic", {}});
    return diagnostics.has_error() ? 0 : 1;
}
