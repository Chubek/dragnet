#pragma once
#include <mlvm/NodeIR/Commo.hpp>

namespace mlvm::nodeir {
// High-level operations; source-language semantics and types are host contracts.
using NodeHIR = Graph<Stage::hir>;
}
