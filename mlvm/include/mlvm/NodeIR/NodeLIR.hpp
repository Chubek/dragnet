#pragma once
#include <mlvm/NodeIR/Commo.hpp>

namespace mlvm::nodeir {
// Low-level operations; target, ABI and register constraints remain explicit.
using NodeLIR = Graph<Stage::lir>;
}
