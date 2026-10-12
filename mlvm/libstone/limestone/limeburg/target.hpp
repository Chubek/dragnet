#pragma once
#include "limeburg.hpp"
#include "unisel/umd.hpp"

namespace limestone::limeburg {
// Shared declarative machine knowledge; the BURS engine never invokes Unisel.
Result<RuleSet> from_umd(const unisel::MachineDescription&);
enum class GraphPolicy { TreeOnly, PreserveShared };
// PreserveShared cuts shared/exported values into forest boundaries. Each
// computation is emitted once; fusion never crosses a boundary or CFG block.
Result<schedrow::Region> select_graph(const unisel::Program&, const RuleSet&, std::string_view root="value", GraphPolicy=GraphPolicy::TreeOnly);
}
