#pragma once
#include "schedrow.hpp"

namespace limestone::schedrow {
struct MotionRequest {
  InstrId instruction=0;
  uint32_t block=0;
  // Empty inserts before the destination terminator, or at the block's end.
  std::optional<InstrId> before;
};
struct MotionOptions {
  size_t work_limit=1000000;
  // Required for instructions with traps, memory, physical state or restricted
  // speculation. The host proves execution-count, environment and effect safety;
  // SSA availability, semantic edges, CFG and group checks remain mandatory.
  std::function<Result<bool>(const Region&,const MotionRequest&)> prove;
};
// Motion is an explicit transformation, followed by ordinary per-block scheduling.
// This verifier requires SSA definitions, unlike the general storage-hazard IL.
Result<int> verify_ssa(const Region&,size_t work_limit=1000000);
Result<Region> move_instruction(const Region&,const MotionRequest&,const MotionOptions& = {});
}
