#pragma once
#include "../limestone/foundation.hpp"
namespace limestone::regtl {
using VReg=uint32_t; using PReg=uint32_t;
struct RegClass { std::string name; std::vector<PReg> members; };
struct Constraint { std::vector<PReg> allowed, forbidden; std::optional<PReg> fixed; std::string bank; };
// Storage units and bit lanes are supplied by the target. Disjoint lanes in one
// unit can coexist; a full-register slice overlaps each of its subregisters.
struct StorageSlice { uint32_t unit=0, begin=0, width=0; };
struct RegisterStorage { PReg id=0; std::string bank; std::vector<StorageSlice> slices; };
struct RegisterTuple { std::vector<VReg> values; std::vector<std::vector<PReg>> alternatives; };
struct LiveRange { VReg value; uint32_t begin,end; std::string klass; Constraint constraint; bool spillable=true; };
struct Allocation { std::unordered_map<VReg,PReg> regs; std::vector<VReg> spilled; };
struct SpillSlot { VReg value; std::string klass; uint64_t offset; uint32_t size,alignment; };
struct Clobber { uint32_t position; std::vector<PReg> registers; };
struct Program {
  std::vector<LiveRange> ranges; std::vector<RegClass> classes;
  // Each pair describes overlapping storage; overlap need not be transitive.
  std::vector<std::pair<PReg,PReg>> aliases;
  std::vector<Clobber> clobbers;
  // CFG analyses supply exact interference instead of convex interval overlap.
  bool explicit_interference=false;
  std::vector<std::pair<VReg,VReg>> interference, ties;
  std::vector<PReg> reserved;
  std::vector<RegisterStorage> storage;
  std::vector<RegisterTuple> tuples;
};
struct VirtualRegister { VReg value; std::string klass; Constraint constraint; bool spillable=true; };
struct TransferOperand {
  enum class Kind { Virtual, Physical, Immediate, Spill, Memory };
  Kind kind; uint32_t id=0; int64_t immediate=0;
  // A memory operand owns its address expression, never a target pointer.
  std::vector<TransferOperand> address;
  bool operator==(const TransferOperand&) const = default;
};
struct Transfer { TransferOperand destination, source; };
struct Instruction {
  uint32_t id; std::vector<VReg> defs, uses, early_defs;
  std::vector<PReg> clobbers;
  std::vector<std::pair<VReg,VReg>> ties;
  // Implicit architectural state has a separate identity/liveness domain.
  std::vector<PReg> physical_defs, physical_uses;
  std::vector<Transfer> transfers;
  bool parallel=false;
  std::string opcode, origin;
};
struct Block { uint32_t id; std::vector<Instruction> instructions; std::vector<uint32_t> successors; std::vector<VReg> live_out; std::vector<PReg> physical_live_out; };
struct Function {
  std::vector<VirtualRegister> values; std::vector<Block> blocks;
  std::vector<RegClass> classes; std::vector<std::pair<PReg,PReg>> aliases;
  std::vector<PReg> reserved;
  std::vector<RegisterStorage> storage;
  std::vector<RegisterTuple> tuples;
};
struct Liveness {
  Program problem;
  std::unordered_map<uint32_t,std::vector<VReg>> live_in, live_out;
  std::unordered_map<VReg,std::vector<std::pair<uint32_t,uint32_t>>> segments;
  std::unordered_map<uint32_t,std::vector<PReg>> physical_live_in, physical_live_out;
  std::unordered_map<uint32_t,std::vector<PReg>> physical_before, physical_after;
};
// Backwards fixed-point dataflow, including loops and disconnected blocks.
Result<Liveness> analyze(const Function&);
Result<Allocation> linear_scan(const Program&);
Result<Allocation> greedy(const Program&);
Result<Allocation> graph_color(const Program&);
// Complete bounded search; a exhausted budget is ResourceLimit, never UNSAT.
Result<Allocation> constraint_allocate(const Program&, size_t search_limit=1000000);
// Costs are an allocator policy, independent of storage/operand legality. An
// omitted register cost is zero; an omitted value uses default_spill_cost.
struct UnaryCost { VReg value; double spill_cost=1; std::vector<std::pair<PReg,double>> registers; };
struct CoalescingCost { VReg first,second; double cost=1; };
struct AllocationCosts { double default_spill_cost=1; std::vector<UnaryCost> values; std::vector<CoalescingCost> coalescing; };
struct PbqpOptions {
  AllocationCosts costs;
  size_t search_limit=1000000, cell_limit=8*1024*1024, work_limit=50000000;
};
struct PbqpStatistics {
  size_t degree_zero=0,degree_one=0,degree_two=0;
  size_t residual_variables=0,search_steps=0,cells=0,work=0;
};
struct PbqpSolution { Allocation allocation; double cost=0; PbqpStatistics statistics; };
// Exact PBQP: unary vectors, quadratic interference/tie/coalescing matrices,
// auxiliary tuple variables, degree-0/1/2 elimination, then bounded search.
// A budget failure is ResourceLimit even if an incumbent exists.
Result<PbqpSolution> solve_pbqp(const Program&,const PbqpOptions& = {});
Result<Allocation> pbqp_allocate(const Program&,const PbqpOptions& = {});
Result<int> validate_costs(const Program&,const AllocationCosts&);
Result<double> allocation_cost(const Program&,const Allocation&,const AllocationCosts& = {});
Result<int> validate(const Program&);
Result<int> verify(const Program&, const Allocation&);
// Shared legality boundary used by every allocator and the verifier.
bool registers_overlap(const Program&,PReg,PReg);
// Canonical pairwise overlaps for consumers with an alias-only storage model.
std::vector<std::pair<PReg,PReg>> register_aliases(const Program&);
std::vector<PReg> allowed_registers(const Program&,const LiveRange&);
// Merge selected fixed-operand/ABI requirements into an owning allocation problem.
// Conflicting identities/assignments fail; existing class/call/allowed/forbidden
// constraints remain authoritative. Fixed ranges cannot be spilled.
Result<Program> with_fixed_registers(const Program&,std::span<const std::pair<VReg,PReg>>);
std::string print(const Program&);
struct Location {
  enum class Kind { Register, Spill };
  Kind kind; uint32_t id;
  bool operator==(const Location&) const = default;
  auto operator<=>(const Location&) const = default;
};
struct Move { Location destination, source; bool operator==(const Move&) const = default; };
// Scratch is caller-owned distinct storage, and may be a register or spill slot.
Result<std::vector<Move>> resolve_parallel_moves(std::span<const Move>, Location scratch);
}
