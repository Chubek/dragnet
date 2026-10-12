#pragma once
#include "regtl.hpp"
#include "schedrow/schedrow.hpp"
#include <map>

namespace limestone::regtl {
struct SpillClass {
  std::string klass,load_opcode,store_opcode,address_space;
  uint32_t size=0,alignment=0;
  std::vector<PReg> scratch;
};
struct AllocatedRegion {
  schedrow::Region region;
  std::vector<uint32_t> order;
  Allocation allocation;
  Program problem;
  std::vector<SpillSlot> slots;
  uint64_t frame_size=0;
  // New short-lived virtual value -> original spilled value.
  std::map<VReg,VReg> value_sources;
  std::vector<schedrow::Scheduled> scheduled;
};
struct SpillOptions {
  size_t work_limit=1000000;
  // Decorate a frame transfer before its scratch allocation. Operands use the
  // original virtual identities here. Additional uses must be existing values;
  // the requested definition/use and private-frame memory effect are mandatory.
  // Returned constraints apply only to this transfer's operands in its unit.
  std::function<Result<std::map<VReg,Constraint>>(schedrow::Instruction&)> configure_transfer;
};
struct SpillAdapter {
  size_t work_limit=1000000;
  // Boundary, control-flow, or multi-instruction transfers use an owning complete
  // reconstruction. The host proves its semantics; the framework independently
  // checks SSA/order, frame bounds, operand classes and physical allocation.
  std::function<Result<AllocatedRegion>(const schedrow::Region&,std::span<const uint32_t>,
    const Program&,const Allocation&,std::span<const SpillClass>,std::span<const VReg>)> lower;
  std::function<Result<int>(const schedrow::Region&,const Allocation&,const AllocatedRegion&)> prove;
};
// Reserve target-supplied scratch storage, including every direct alias, before
// allocation. Spills use a stable private frame, never guessed stack offsets.
Result<Program> reserve_spill_registers(const Program&,std::span<const SpillClass>);
// Extend operand lifetimes to every possible recipe use before allocation.
// Recipes are explicitly target-approved pure SSA definitions. Operands may
// themselves spill or be rematerialized; no architectural effects are inferred.
Result<Program> prepare_rematerialization(const schedrow::Region&,std::span<const uint32_t> order,
  const Program&,std::span<const VReg> recipes,std::span<const VReg> outputs={},size_t work_limit=1000000);
Result<AllocatedRegion> materialize_spills(const schedrow::Region&,std::span<const uint32_t> order,
  const Program&,const Allocation&,std::span<const SpillClass>,std::span<const VReg> outputs={},
  std::span<const VReg> rematerializable={},const SpillOptions& = {});
Result<AllocatedRegion> materialize_spills(const schedrow::Region&,std::span<const uint32_t> order,
  const Program&,const Allocation&,std::span<const SpillClass>,std::span<const VReg> outputs,
  const SpillAdapter&);
// Add post-allocation storage hazards without changing semantic operands.
Result<schedrow::Region> allocated_dependencies(const schedrow::Region&,const Allocation&,
  std::span<const std::pair<PReg,PReg>> aliases={});
}
