#pragma once
#include "../limestone/foundation.hpp"
#include "../unisel/unisel.hpp"
#include "../regtl/regtl.hpp"
#include "../regtl/scheduling_adapter.hpp"
#include "../unisel/umd.hpp"
#include "../limeburg/limeburg.hpp"
#include "../limeburg/target.hpp"
#include "../traceml/traceml.hpp"
#include "../bin2bin/object.hpp"
#include "../bin2bin/bin2bin.hpp"
#include "../schedrow/motion.hpp"
namespace limestone {
enum class SelectionStrategy { Global, Greedy, BURS };
enum class AllocationStrategy { LinearScan, Greedy, GraphColoring, Constraint, PBQP };
// Physical allocation requires an explicit target register model.
struct PipelineOptions { bool optimize=true, schedule=true, allocate=false; SelectionStrategy selector=SelectionStrategy::Global; AllocationStrategy allocator=AllocationStrategy::LinearScan; bool encode=false; bool trace_execution=false; regtl::PbqpOptions pbqp;
  std::vector<schedrow::MotionRequest> motion;
  size_t motion_work_limit=1000000;
};
struct InstructionModel {
  std::optional<uint32_t> latency;
  double throughput=1;
  std::vector<schedrow::ResourceUse> resources;
  std::string opcode_class, semantic_class;
  bool barrier=false, memory=false;
  std::vector<uint32_t> implicit_defs, implicit_uses;
  std::optional<schedrow::MemoryAccess> access;
  bool call=false, terminator=false, may_trap=false;
  std::vector<uint32_t> issue_slots;
  uint32_t issue_width=1;
  std::vector<uint32_t> early_definitions;
  // Definition/use operand indices, resolved only after selection.
   std::vector<std::pair<uint32_t,uint32_t>> ties;
   // Definition operand index -> result latency, resolved after selection.
   std::unordered_map<uint32_t,uint32_t> result_latencies;
    schedrow::ControlFlow control=schedrow::ControlFlow::None;
     std::unordered_map<uint32_t,uint32_t> implicit_result_latencies;
     bool speculative=true;
     int priority=0;
     std::unordered_map<std::string,int> pressure_delta;
      // Preserve the source contract independently of normalized scheduling data.
      metacode::Value::Object metadata;
      // Selected operand index -> required physical identity. These constrain
      // allocation; they are not architectural implicit state definitions/uses.
       std::map<uint32_t,uint32_t> fixed_definitions, fixed_uses;
        // Target proof that a pure SSA definition can be recomputed. Recipe
        // operand lifetimes are extended before physical allocation.
        bool rematerializable=false;
        std::optional<schedrow::LatencyRange> latency_range, memory_latency;
        std::unordered_map<uint32_t,schedrow::LatencyRange> result_latency_ranges, implicit_result_latency_ranges;
        // Explicit results are definition indices here, SSA identities after
        // selection. Architectural result IDs remain physical identities.
        std::vector<schedrow::OperandLatency> operand_latencies;
};
struct Module;
struct Relocation { size_t offset; std::string kind, symbol; int64_t addend=0; };
struct BackendOutput { std::vector<uint8_t> bytes; std::vector<Relocation> relocations; };
struct PipelineTarget {
  std::string name;
  std::vector<unisel::Pattern> patterns;
  schedrow::MachineModel scheduling;
  std::unordered_map<std::string,InstructionModel> instructions;
  std::vector<regtl::RegClass> register_classes;
  std::vector<std::pair<regtl::PReg,regtl::PReg>> aliases;
  std::unordered_map<uint32_t,std::string> value_classes;
  std::unordered_map<uint32_t,regtl::Constraint> constraints;
  std::string default_register_class;
  // A verified IL adapter owns optimization semantics; orchestration invokes it.
  std::function<Result<unisel::Program>(const unisel::Program&)> optimizer;
  std::optional<limeburg::RuleSet> burs_rules;
  std::function<Result<regtl::Program>(const unisel::Program&,const schedrow::Region&,std::span<const uint32_t>)> allocation_adapter;
   std::function<Result<BackendOutput>(const Module&)> backend;
    limeburg::GraphPolicy burs_policy=limeburg::GraphPolicy::PreserveShared;
     std::vector<regtl::SpillClass> spill_classes;
     // Resolve target grouping over selected identities without altering semantics.
      std::function<Result<std::vector<schedrow::InstructionGroup>>(const unisel::Program&,const schedrow::Region&)> grouping_adapter;
       metacode::Value::Object metadata;
       std::vector<regtl::RegisterStorage> register_storage;
        std::vector<regtl::RegisterTuple> register_tuples;
        // Semantic proof for requested cross-block motion with restricted
        // speculation/effects. SSA and dependence legality are always checked.
         decltype(schedrow::MotionOptions::prove) motion_proof;
          regtl::SpillOptions spill_options;
          std::optional<regtl::SpillAdapter> spill_adapter;
};
struct Module {
  std::string name; std::string machine_ir;
  schedrow::Region selected;
  std::vector<schedrow::Scheduled> scheduled;
  regtl::Program allocation_problem;
  std::optional<regtl::Allocation> allocation;
  std::vector<std::string> stages;
  std::optional<BackendOutput> encoded;
  std::string target;
  unisel::Program optimized;
   std::optional<traceml::ExecutionResult> execution;
   // Versioned single-region handoff to the D MachineIR package.
    std::string machine_ir_exchange;
    // Stable block-layout order, including equal-cycle dependency ordering.
     std::vector<uint32_t> order;
     // Allocation retains the original problem; materialization owns transfers,
     // scratch assignments, final order, and the private spill-frame contract.
     std::optional<regtl::AllocatedRegion> materialized;
};
Result<PipelineTarget> make_target(const unisel::MachineDescription&);
// Infobank ingress also installs the metadata codec when its contract is supported.
Result<PipelineTarget> make_target(const metacode::Architecture&);
Result<PipelineTarget> make_target(const metacode::Architecture&,const bin2bin::CodecAdapter&);
// Source convenience entry: closed TraceML integer computations -> portable IR.
Result<Module> run_pipeline(std::string_view input, const PipelineOptions& options = PipelineOptions{});
// Closed TraceML evaluation/trace lowering followed by the explicit target's
// selection/allocation/backend pipeline. The target must implement the lowering.
Result<Module> run_pipeline(std::string_view,const PipelineTarget&,const PipelineOptions&);
// Explicit machine-independent graph -> target selection/scheduling/allocation.
Result<Module> run_pipeline(const unisel::Program&, const PipelineTarget&, const PipelineOptions& = {});
Result<bin2bin::ObjectFile> make_object(const Module&,const bin2bin::ObjectTarget&,std::string_view symbol);
}
