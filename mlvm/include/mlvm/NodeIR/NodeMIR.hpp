#pragma once
#include <mlvm/NodeIR/Commo.hpp>

namespace mlvm::nodeir {
// Explicit data/control edges; SSA and dominance are required capabilities.
using NodeMIR = Graph<Stage::mir>;
}
