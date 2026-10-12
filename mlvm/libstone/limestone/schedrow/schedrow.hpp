#pragma once
#include "../limestone/foundation.hpp"
#include "../metacode/operand_constraints.hpp"
namespace limestone::schedrow {
using InstrId=uint32_t; using ValueId=uint32_t;
enum class DepKind { True, Anti, Output, Memory, Control, Ordering };
// Bounds are retained for inspection; static schedules use the maximum. An
// optimistic/observed latency needs a separate runtime recovery contract.
struct LatencyRange { uint32_t minimum=0, maximum=0; bool operator==(const LatencyRange&) const = default; };
struct Dependency { InstrId producer, consumer; DepKind kind; uint32_t latency=0, distance=0; bool scheduler_only=false; std::optional<LatencyRange> latency_range; };
// Producer result -> a particular use of a concrete consumer opcode. SSA and
// architectural results retain separate identities, including after allocation.
struct OperandLatency { ValueId result=0; std::string consumer_opcode; uint32_t use_index=0; LatencyRange cycles; bool implicit=false; };
struct ResourceUse { std::string resource; uint32_t duration=1; double quantity=1; uint32_t offset=0; std::vector<std::string> alternatives; };
enum class MemoryOrdering { Relaxed, Acquire, Release, AcquireRelease, Sequential };
enum class ControlFlow { None, Branch, ConditionalBranch, Return, IndirectBranch, Trap, Call };
struct MemoryAccess {
  bool read=false, write=false, volatile_access=false, atomic=false;
  MemoryOrdering ordering=MemoryOrdering::Relaxed;
  std::string address_space;
  std::vector<uint32_t> alias_sets;
  uint32_t size=0, alignment=0;
};
struct Instruction {
 InstrId id; std::string opcode, opcode_class, semantic_class;
 std::vector<ValueId> defs, uses; std::vector<ResourceUse> resources;
  uint32_t latency=1; double throughput=1.0; bool barrier=false, speculative=true, memory=false;
  std::vector<ValueId> implicit_defs, implicit_uses;
   std::string origin;
   // Constants consumed by a pattern retain their source value identities.
    std::vector<std::pair<ValueId,int64_t>> immediates;
    std::optional<MemoryAccess> access;
    bool terminator=false, call=false, may_trap=false;
    // Per-result latency overrides the default result latency.
    std::unordered_map<ValueId,uint32_t> result_latency;
    std::vector<uint32_t> issue_slots;
    int priority=0;
     std::unordered_map<std::string,int> pressure_delta;
     std::unordered_map<ValueId,std::string> register_classes;
     std::vector<ValueId> early_defs;
      std::vector<std::pair<ValueId,ValueId>> ties;
      uint32_t block=0;
      std::vector<uint32_t> block_targets;
       ControlFlow control=ControlFlow::None;
       // Architectural register IDs occupy a separate latency domain from SSA IDs.
        std::unordered_map<ValueId,uint32_t> implicit_result_latency;
        // Number of distinct issue slots consumed in the issue cycle.
         uint32_t issue_width=1;
         std::optional<LatencyRange> latency_range, memory_latency;
         std::unordered_map<ValueId,LatencyRange> result_latency_ranges, implicit_result_latency_ranges;
          std::vector<OperandLatency> operand_latencies;
          // Covered source identities retain target-defined literals/properties.
          std::map<ValueId,metacode::OperandMetadata> source_metadata;
};
LatencyRange latency_bounds(const Instruction&);
LatencyRange latency_bounds(const Instruction&,ValueId result,bool implicit,const Instruction* consumer=nullptr,uint32_t use_index=0);
struct BasicBlock { uint32_t id; std::string name; std::vector<uint32_t> successors; std::vector<ValueId> live_out; };
enum class GroupKind { Ordered, Adjacent, SameCycle, Bundle, Atomic, Fusion, Pair };
// Members are in emission order. Groups may overlap within a block. Fusion preserves
// adjacency and target hints without changing instruction semantics or resources.
struct InstructionGroup {
  uint32_t id; GroupKind kind=GroupKind::Ordered; std::vector<InstrId> members;
  std::string name, origin, pattern; int benefit=0;
  uint32_t issue_width=0; std::vector<uint32_t> issue_slots;
};
struct Region { std::string name; std::vector<Instruction> instructions; std::vector<Dependency> deps; std::vector<BasicBlock> blocks; uint32_t entry=0; std::vector<InstructionGroup> groups; };
struct MachineModel { std::unordered_map<std::string,uint32_t> resource_capacity; uint32_t issue_width=0; bool critical_path=false; std::vector<std::pair<uint32_t,uint32_t>> register_aliases; size_t group_search_limit=1000000; size_t verification_limit=1000000; };
struct Scheduled { InstrId id; uint32_t cycle; std::optional<uint32_t> slot; std::vector<std::string> resources; std::vector<uint32_t> additional_slots; };
// Structural/effect contracts, independent of a microarchitecture or schedule.
Result<int> validate_region(const Region&);
// Check a sequential emission order against semantic and generated hazards,
// including zero-latency edges and the declared CFG block layout.
Result<int> verify_order(const Region&,std::span<const InstrId>,std::span<const std::pair<uint32_t,uint32_t>> physical_aliases={});
// Group-only contracts, usable at machine handoffs without guessing capacities.
Result<int> verify_group_order(const Region&,std::span<const InstrId>);
Result<int> verify_groups(const Region&,std::span<const Scheduled>);
Result<std::vector<Scheduled>> schedule(const Region&, const MachineModel&);
Result<int> verify(const Region&, const MachineModel&, std::span<const Scheduled>);
// Per-block issue cycles and resources; block declaration order is the layout.
Result<std::vector<Scheduled>> schedule_cfg(const Region&,const MachineModel&);
Result<int> verify_cfg(const Region&,const MachineModel&,std::span<const Scheduled>);
Result<Region> block_region(const Region&,uint32_t block);
// Augments explicit edges with register hazards and scheduling barriers.
Result<Region> dependencies(const Region&,std::span<const std::pair<uint32_t,uint32_t>> physical_aliases={});
std::string print(const Region&);
struct ModuloOptions { uint32_t initiation_interval=1, max_cycle=64; size_t search_limit=1000000; };
Result<std::vector<Scheduled>> schedule_modulo(const Region&, const MachineModel&, ModuloOptions = {});
Result<int> verify_modulo(const Region&, const MachineModel&, std::span<const Scheduled>, uint32_t initiation_interval);
}
