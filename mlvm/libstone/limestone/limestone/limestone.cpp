#include "limestone.hpp"
#include "../traceml/traceml.hpp"
#include "../limeburg/target.hpp"
#include "metacode/machine-ir/bridge.hpp"
#include <limits>
#include <map>
#include <sstream>
#include <set>

namespace limestone {
Result<bin2bin::ObjectFile> make_object(const Module& module,const bin2bin::ObjectTarget& target,std::string_view symbol) {
  if(!module.encoded)return Result<bin2bin::ObjectFile>::err({Error::Code::InvalidArgument,"object emission requires an encoded module"});
  if(!target.architecture.empty()&&module.target!=target.architecture)return Result<bin2bin::ObjectFile>::err({Error::Code::Conflict,"encoded module and object target architectures differ"});
  std::vector<bin2bin::NamedRelocation> relocations;for(auto& r:module.encoded->relocations)relocations.push_back({r.offset,r.kind,r.symbol,r.addend});
  return bin2bin::code_object(target,module.encoded->bytes,symbol,relocations);
}
namespace {
Error stage(std::string_view name,const Error& error) {
  return {error.code,std::string(name)+": "+error.message};
}
Result<int> timing(schedrow::Instruction& instruction,const InstructionModel& model) {
  if(model.latency)instruction.latency=*model.latency;
  instruction.latency_range=model.latency_range;instruction.memory_latency=model.memory_latency;
  instruction.implicit_result_latency=model.implicit_result_latencies;instruction.implicit_result_latency_ranges=model.implicit_result_latency_ranges;
  instruction.result_latency.clear();instruction.result_latency_ranges.clear();instruction.operand_latencies.clear();
  for(auto [index,cycles]:model.result_latencies){if(index>=instruction.defs.size())return Result<int>::err({Error::Code::Conflict,"target result-latency index out of range"});instruction.result_latency[instruction.defs[index]]=cycles;}
  for(auto [index,cycles]:model.result_latency_ranges){if(index>=instruction.defs.size())return Result<int>::err({Error::Code::Conflict,"target result-latency range index out of range"});instruction.result_latency_ranges[instruction.defs[index]]=cycles;}
  for(auto operand:model.operand_latencies){if(!operand.implicit){if(operand.result>=instruction.defs.size())return Result<int>::err({Error::Code::Conflict,"target operand-latency result index out of range"});operand.result=instruction.defs[operand.result];}instruction.operand_latencies.push_back(std::move(operand));}
  return Result<int>::ok(0);
}
Result<regtl::Program> allocation_problem(const unisel::Program& source,const PipelineTarget& target,const schedrow::Region& region,const std::vector<uint32_t>& order) {
  regtl::Function function;function.classes=target.register_classes;function.aliases=target.aliases;
  function.storage=target.register_storage;function.tuples=target.register_tuples;
  std::map<uint32_t,regtl::Block> blocks;
  if(region.blocks.empty()){regtl::Block block{};block.id=0;block.live_out=source.outputs;blocks[0]=std::move(block);}
  else for(auto& b:region.blocks){regtl::Block block{b.id,{},b.successors,b.live_out};if(b.successors.empty())block.live_out.insert(block.live_out.end(),source.outputs.begin(),source.outputs.end());blocks[b.id]=std::move(block);}
  std::map<uint32_t,const schedrow::Instruction*> instructions;
  for(auto& instruction:region.instructions)instructions[instruction.id]=&instruction;
  std::set<uint32_t> values,defined;
  std::map<uint32_t,std::string> source_classes;
  for(auto& n:source.nodes){if(!n.register_class.empty())source_classes[n.id]=n.register_class;if(!n.required&&n.produces_value){values.insert(n.id);defined.insert(n.id);}}
  if(order.size()>std::numeric_limits<uint32_t>::max()/2)return Result<regtl::Program>::err({Error::Code::ResourceLimit,"too many allocation positions"});
  for(size_t k=0;k<order.size();++k) {
    auto& instruction=*instructions.at(order[k]);
    for(auto value:instruction.uses) {
      if(region.blocks.empty()&&!defined.contains(value))return Result<regtl::Program>::err({Error::Code::Conflict,"use before definition of v"+std::to_string(value)});values.insert(value);
    }
    for(auto value:instruction.defs){if(!defined.insert(value).second)return Result<regtl::Program>::err({Error::Code::Conflict,"multiple definitions of v"+std::to_string(value)});values.insert(value);}
    for(auto& [value,klass]:instruction.register_classes){if(source_classes.contains(value)&&source_classes.at(value)!=klass)return Result<regtl::Program>::err({Error::Code::Conflict,"conflicting selected register classes"});source_classes[value]=klass;}
    blocks.at(region.blocks.empty()?0:instruction.block).instructions.push_back({instruction.id,instruction.defs,instruction.uses,instruction.early_defs,instruction.implicit_defs,instruction.ties,instruction.implicit_defs,instruction.implicit_uses});
  }
  for(auto& [id,block]:blocks)values.insert(block.live_out.begin(),block.live_out.end());
  for(auto value:values) {
    auto it=target.value_classes.find(value);auto klass=it==target.value_classes.end()?(source_classes.contains(value)?source_classes.at(value):target.default_register_class):it->second;
    if(it!=target.value_classes.end()&&source_classes.contains(value)&&source_classes.at(value)!=klass)return Result<regtl::Program>::err({Error::Code::Conflict,"target allocation class disagrees with selected operand class"});
    if(klass.empty())return Result<regtl::Program>::err({Error::Code::Unsupported,"missing target register class for v"+std::to_string(value)});
    auto constraint=target.constraints.find(value);
    bool boundary=std::find(source.outputs.begin(),source.outputs.end(),value)!=source.outputs.end()||std::any_of(source.nodes.begin(),source.nodes.end(),[&](auto& n){return n.id==value&&!n.required;});for(auto& b:source.blocks)boundary|=std::find(b.live_out.begin(),b.live_out.end(),value)!=b.live_out.end();
    function.values.push_back({value,klass,constraint==target.constraints.end()?regtl::Constraint{}:constraint->second,!boundary||bool(target.spill_adapter)});
  }
  for(auto& [id,block]:blocks)function.blocks.push_back(std::move(block));auto analysis=regtl::analyze(function);if(!analysis)return Result<regtl::Program>::err(analysis.error());return Result<regtl::Program>::ok(std::move(analysis.value().problem));
}
std::string emit(const Module& module,const std::vector<uint32_t>& order) {
  const auto& region=module.materialized?module.materialized->region:module.selected;const auto& allocation=module.materialized?std::optional<regtl::Allocation>(module.materialized->allocation):module.allocation;
  std::ostringstream out;out<<"module "<<module.name<<" {\n  function main {\n";
  auto blocks=region.blocks;if(blocks.empty())blocks.push_back({0,"entry"});
  for(auto& block:blocks) {
  out<<"    block "<<(block.name.empty()?"bb"+std::to_string(block.id):block.name)<<" {\n";
  if(allocation) {
    std::map<uint32_t,uint32_t> registers(allocation->regs.begin(),allocation->regs.end());
    for(auto [value,physical]:registers)out<<"      ; v"<<value<<" -> physical "<<physical<<'\n';
    for(auto value:allocation->spilled)out<<"      ; v"<<value<<" -> spill (materialization required)\n";
    if(module.materialized)for(auto& slot:module.materialized->slots)out<<"      ; spill v"<<slot.value<<" at frame+"<<slot.offset<<" size="<<slot.size<<" alignment="<<slot.alignment<<'\n';
  }
  std::map<uint32_t,uint32_t> cycles;
  for(auto scheduled:module.materialized?module.materialized->scheduled:module.scheduled)cycles[scheduled.id]=scheduled.cycle;
  for(auto id:order) {
    const auto& i=*std::find_if(region.instructions.begin(),region.instructions.end(),[&](auto& x){return x.id==id;});
    if(!region.blocks.empty()&&i.block!=block.id)continue;
    out<<"      ";for(size_t n=0;n<i.defs.size();++n){if(n)out<<", ";out<<'%'<<i.defs[n];}
    if(!i.defs.empty())out<<" = ";out<<i.opcode;
    for(auto value:i.uses)out<<" %"<<value;
    for(auto [value,constant]:i.immediates)out<<" #"<<constant;
    for(auto target:i.block_targets)out<<" block %"<<target;
    if(cycles.contains(id))out<<" ; cycle "<<cycles.at(id);
    out<<'\n';
  }
  out<<"    }\n";
  }
  return out.str()+"  }\n}\n";
}
}
Result<Module> run_pipeline(const unisel::Program& input,const PipelineTarget& target,const PipelineOptions& options) {
  std::string_view active_stage="ingest";
  try {
  if(target.name.empty())return Result<Module>::err({Error::Code::InvalidArgument,"target has no identity"});
   Module module;module.name="module";module.target=target.name;auto prepared=unisel::prepare(input);if(!prepared)return Result<Module>::err(stage("ingest",prepared.error()));unisel::Program program=std::move(prepared.value());
  auto valid=unisel::validate(program,target.patterns);if(!valid)return Result<Module>::err(stage("ingest",valid.error()));
  module.stages.push_back("ingest");
  if(options.optimize&&target.optimizer) {
    active_stage="optimize";
    auto optimized=target.optimizer(program);if(!optimized)return Result<Module>::err(stage("optimize",optimized.error()));
    program=std::move(optimized.value());valid=unisel::validate(program,target.patterns);
    if(!valid)return Result<Module>::err(stage("optimize",valid.error()));module.stages.push_back("optimize");
  }
    module.optimized=program;
    active_stage="select";
   auto region=[&]()->Result<schedrow::Region> {
     if(options.selector==SelectionStrategy::BURS) {
       if(!target.burs_rules)return Result<schedrow::Region>::err({Error::Code::Unsupported,"target has no BURS rules"});
        return limeburg::select_graph(program,*target.burs_rules,"value",target.burs_policy);
     }
     if(options.selector!=SelectionStrategy::Global&&options.selector!=SelectionStrategy::Greedy)return Result<schedrow::Region>::err({Error::Code::InvalidArgument,"unknown selector"});
     auto selected=options.selector==SelectionStrategy::Global?unisel::solve(program,target.patterns):unisel::solve_greedy(program,target.patterns);
     if(!selected)return Result<schedrow::Region>::err(selected.error());return unisel::emit_scheduler(program,target.patterns,selected.value());
   }();if(!region)return Result<Module>::err(stage("select",region.error()));
  module.selected=std::move(region.value());module.stages.push_back("select");
   std::vector<std::pair<regtl::VReg,regtl::PReg>> fixed_registers;
   std::vector<regtl::VReg> rematerializable;
  for(auto& i:module.selected.instructions) {
    auto it=target.instructions.find(i.opcode);
    if(it==target.instructions.end()) {
       return Result<Module>::err({Error::Code::Unsupported,"select: missing target instruction model for "+i.opcode});
    }
      auto& model=it->second;
      if(model.rematerializable)rematerializable.insert(rematerializable.end(),i.defs.begin(),i.defs.end());
      for(auto [index,physical]:model.fixed_definitions){if(index>=i.defs.size())return Result<Module>::err({Error::Code::Conflict,"target fixed definition index out of range: "+i.opcode});fixed_registers.emplace_back(i.defs[index],physical);}
      for(auto [index,physical]:model.fixed_uses){if(index>=i.uses.size())return Result<Module>::err({Error::Code::Conflict,"target fixed use index out of range: "+i.opcode});fixed_registers.emplace_back(i.uses[index],physical);}
     if((model.memory||model.access)&&!i.memory)return Result<Module>::err({Error::Code::Conflict,"target adds memory effects to selected semantics: "+i.opcode});
     if((model.call&&!i.call)||(model.terminator&&!i.terminator)||(model.may_trap&&!i.may_trap))return Result<Module>::err({Error::Code::Conflict,"target adds call, termination, or trap effects to selected semantics: "+i.opcode});
     if(model.control!=schedrow::ControlFlow::None&&model.control!=i.control)return Result<Module>::err({Error::Code::Conflict,"target control flow disagrees with selected semantics: "+i.opcode});
     if(options.schedule&&!model.latency&&!model.latency_range)return Result<Module>::err({Error::Code::Unsupported,"schedule: unknown target latency for "+i.opcode});
     auto timed=timing(i,model);if(!timed)return Result<Module>::err(stage("select",timed.error()));
     i.throughput=model.throughput;i.resources=model.resources;i.opcode_class=model.opcode_class;i.semantic_class=model.semantic_class;
      i.speculative&=model.speculative;i.priority=model.priority;i.pressure_delta=model.pressure_delta;
      i.barrier|=model.barrier;i.memory|=model.memory;i.implicit_defs=model.implicit_defs;i.implicit_uses=model.implicit_uses;
      i.implicit_result_latency=model.implicit_result_latencies;
      if(model.access){if(!i.access)return Result<Module>::err({Error::Code::Conflict,"target memory effects need explicit selected semantics"});auto& a=*i.access;auto& b=*model.access;if(a.read!=b.read||a.write!=b.write||a.volatile_access!=b.volatile_access||a.atomic!=b.atomic||a.ordering!=b.ordering||(!b.address_space.empty()&&a.address_space!=b.address_space)||(b.size&&a.size!=b.size)||(b.alignment&&(!a.alignment||a.alignment<b.alignment)))return Result<Module>::err({Error::Code::Conflict,"target memory effects disagree with selected semantics"});}
      i.call|=model.call;i.terminator|=model.terminator;i.may_trap|=model.may_trap;i.issue_slots=model.issue_slots;
      i.issue_width=model.issue_width;
      for(auto index:model.early_definitions){if(index>=i.defs.size())return Result<Module>::err({Error::Code::Conflict,"target early-definition index out of range"});i.early_defs.push_back(i.defs[index]);}
       for(auto [def,use]:model.ties){if(def>=i.defs.size()||use>=i.uses.size())return Result<Module>::err({Error::Code::Conflict,"target tied operand index out of range"});i.ties.emplace_back(i.defs[def],i.uses[use]);}
       if(model.control!=schedrow::ControlFlow::None){if(i.control!=schedrow::ControlFlow::None&&i.control!=model.control)return Result<Module>::err({Error::Code::Conflict,"target control flow disagrees with selected semantics"});i.control=model.control;i.call|=i.control==schedrow::ControlFlow::Call;i.terminator|=i.control!=schedrow::ControlFlow::Call;}
  }
    active_stage="group";
    if(target.grouping_adapter){auto groups=target.grouping_adapter(program,module.selected);if(!groups)return Result<Module>::err(stage("group",groups.error()));module.selected.groups.insert(module.selected.groups.end(),groups.value().begin(),groups.value().end());if(!groups.value().empty())module.stages.push_back("group");}
     valid=schedrow::validate_region(module.selected);if(!valid)return Result<Module>::err(stage("select",valid.error()));
     if(!options.motion.empty()) {
       active_stage="motion";schedrow::MotionOptions motion;motion.work_limit=options.motion_work_limit;motion.prove=target.motion_proof;
       for(auto& request:options.motion){auto moved=schedrow::move_instruction(module.selected,request,motion);if(!moved)return Result<Module>::err(stage("motion",moved.error()));module.selected=std::move(moved.value());}
       module.stages.push_back("motion");
     }
   std::vector<uint32_t> order;
  if(options.schedule) {
    active_stage="schedule";
    auto scheduled=schedrow::schedule(module.selected,target.scheduling);if(!scheduled)return Result<Module>::err(stage("schedule",scheduled.error()));
    module.scheduled=std::move(scheduled.value());auto checked=schedrow::verify(module.selected,target.scheduling,module.scheduled);
    if(!checked)return Result<Module>::err(stage("schedule",checked.error()));
    for(auto i:module.scheduled)order.push_back(i.id);module.stages.push_back("schedule");
  }else if(module.selected.blocks.empty())for(auto& i:module.selected.instructions)order.push_back(i.id);
  else for(auto& b:module.selected.blocks)for(auto& i:module.selected.instructions)if(i.block==b.id)order.push_back(i.id);
  module.order=order;
  if(options.allocate) {
     active_stage="allocate";
      auto problem=target.allocation_adapter?target.allocation_adapter(program,module.selected,order):allocation_problem(program,target,module.selected,order);if(!problem)return Result<Module>::err(stage("allocate",problem.error()));
       if(!fixed_registers.empty()){auto constrained=regtl::with_fixed_registers(problem.value(),fixed_registers);if(!constrained)return Result<Module>::err(stage("allocate",constrained.error()));problem=std::move(constrained);}
       if(!rematerializable.empty()){auto extended=regtl::prepare_rematerialization(module.selected,order,problem.value(),rematerializable,program.outputs);if(!extended)return Result<Module>::err(stage("allocate",extended.error()));problem=std::move(extended);}
     if(!target.spill_classes.empty()){auto reserved=regtl::reserve_spill_registers(problem.value(),target.spill_classes);if(!reserved)return Result<Module>::err(stage("allocate",reserved.error()));problem=std::move(reserved);}
     module.allocation_problem=std::move(problem.value());auto allocated=[&]()->Result<regtl::Allocation>{
       switch(options.allocator){case AllocationStrategy::LinearScan:return regtl::linear_scan(module.allocation_problem);case AllocationStrategy::Greedy:return regtl::greedy(module.allocation_problem);case AllocationStrategy::GraphColoring:return regtl::graph_color(module.allocation_problem);case AllocationStrategy::Constraint:return regtl::constraint_allocate(module.allocation_problem);case AllocationStrategy::PBQP:return regtl::pbqp_allocate(module.allocation_problem,options.pbqp);}return Result<regtl::Allocation>::err({Error::Code::InvalidArgument,"unknown allocator"});
     }();
     if(!allocated)return Result<Module>::err(stage("allocate",allocated.error()));
     auto verified=regtl::verify(module.allocation_problem,allocated.value());if(!verified)return Result<Module>::err(stage("allocate",verified.error()));
     module.allocation=std::move(allocated.value());module.stages.push_back("allocate");
       if(!module.allocation->spilled.empty()&&(!target.spill_classes.empty()||target.spill_adapter)) {
         active_stage="spill";
        auto spill_options=target.spill_options;auto configure=spill_options.configure_transfer;
        spill_options.configure_transfer=[&](schedrow::Instruction& i)->Result<std::map<regtl::VReg,regtl::Constraint>>{
          std::map<regtl::VReg,regtl::Constraint> constraints;
          if(configure){auto configured=configure(i);if(!configured)return configured;constraints=std::move(configured.value());}
          auto found=target.instructions.find(i.opcode);if(found==target.instructions.end())return Result<std::map<regtl::VReg,regtl::Constraint>>::err({Error::Code::Unsupported,"missing transfer instruction model: "+i.opcode});auto& model=found->second;
          if(model.call||model.terminator||model.control!=schedrow::ControlFlow::None)return Result<std::map<regtl::VReg,regtl::Constraint>>::err({Error::Code::Unsupported,"control transfer requires a spill reconstruction adapter"});
          i.implicit_defs=model.implicit_defs;i.implicit_uses=model.implicit_uses;
          for(auto index:model.early_definitions){if(index>=i.defs.size())return Result<std::map<regtl::VReg,regtl::Constraint>>::err({Error::Code::Conflict,"transfer early-definition index out of range"});i.early_defs.push_back(i.defs[index]);}
          for(auto [def,use]:model.ties){if(def>=i.defs.size()||use>=i.uses.size())return Result<std::map<regtl::VReg,regtl::Constraint>>::err({Error::Code::Conflict,"transfer tied-operand index out of range"});i.ties.emplace_back(i.defs[def],i.uses[use]);}
          for(auto* fixed:{&model.fixed_definitions,&model.fixed_uses})for(auto [index,reg]:*fixed){auto& operands=fixed==&model.fixed_definitions?i.defs:i.uses;if(index>=operands.size())return Result<std::map<regtl::VReg,regtl::Constraint>>::err({Error::Code::Conflict,"transfer fixed-operand index out of range"});auto& constraint=constraints[operands[index]];if(constraint.fixed&&constraint.fixed!=reg)return Result<std::map<regtl::VReg,regtl::Constraint>>::err({Error::Code::Conflict,"conflicting transfer fixed register"});constraint.fixed=reg;}
          auto timed=timing(i,model);if(!timed)return Result<std::map<regtl::VReg,regtl::Constraint>>::err(timed.error());return Result<std::map<regtl::VReg,regtl::Constraint>>::ok(std::move(constraints));
        };
        auto final=target.spill_adapter?regtl::materialize_spills(module.selected,order,module.allocation_problem,*module.allocation,target.spill_classes,program.outputs,*target.spill_adapter):regtl::materialize_spills(module.selected,order,module.allocation_problem,*module.allocation,target.spill_classes,program.outputs,rematerializable,spill_options);if(!final)return Result<Module>::err(stage("spill",final.error()));
        for(auto& i:final.value().region.instructions)if(std::none_of(module.selected.instructions.begin(),module.selected.instructions.end(),[&](auto& selected){return selected.id==i.id;})){
           auto model=target.instructions.find(i.opcode);if(model!=target.instructions.end()&&model->second.access){if(!i.access)return Result<Module>::err({Error::Code::Conflict,"spill: missing transfer memory semantics"});auto& a=*i.access;auto& b=*model->second.access;if(a.read!=b.read||a.write!=b.write||a.atomic!=b.atomic||a.volatile_access!=b.volatile_access||a.ordering!=b.ordering||(!b.address_space.empty()&&a.address_space!=b.address_space)||(b.size&&a.size!=b.size)||(b.alignment&&a.alignment<b.alignment))return Result<Module>::err({Error::Code::Conflict,"spill: transfer model disagrees with private-frame semantics"});}
            auto found=target.instructions.find(i.opcode);if(found==target.instructions.end()||(options.schedule&&!found->second.latency&&!found->second.latency_range))return Result<Module>::err({Error::Code::Unsupported,"spill: missing transfer instruction model for "+i.opcode});auto& m=found->second;
            for(auto* fixed:{&m.fixed_definitions,&m.fixed_uses})for(auto [index,physical]:*fixed){auto& operands=fixed==&m.fixed_definitions?i.defs:i.uses;if(index>=operands.size()||!final.value().allocation.regs.contains(operands[index])||final.value().allocation.regs.at(operands[index])!=physical)return Result<Module>::err({Error::Code::Conflict,"spill: transfer fixed register was not preserved"});}
            if(i.implicit_defs!=m.implicit_defs||i.implicit_uses!=m.implicit_uses)return Result<Module>::err({Error::Code::Conflict,"spill: transfer architectural operands disagree with target model"});
            for(auto index:m.early_definitions)if(index>=i.defs.size()||std::find(i.early_defs.begin(),i.early_defs.end(),i.defs[index])==i.early_defs.end())return Result<Module>::err({Error::Code::Conflict,"spill: missing early definition"});
            for(auto [def,use]:m.ties)if(def>=i.defs.size()||use>=i.uses.size()||std::find(i.ties.begin(),i.ties.end(),std::pair{i.defs[def],i.uses[use]})==i.ties.end())return Result<Module>::err({Error::Code::Conflict,"spill: missing tied operands"});
            auto timed=timing(i,m);if(!timed)return Result<Module>::err(stage("spill",timed.error()));i.throughput=m.throughput;i.resources=m.resources;i.issue_slots=m.issue_slots;i.semantic_class=m.semantic_class;i.opcode_class=m.opcode_class;i.barrier|=m.barrier;i.may_trap|=m.may_trap;
          i.speculative&=m.speculative;i.priority=m.priority;i.pressure_delta=m.pressure_delta;
           i.issue_width=m.issue_width;
       }
       module.materialized=std::move(final.value());order=module.materialized->order;module.order=order;
        auto overlaps=regtl::register_aliases(module.materialized->problem);auto hazards=regtl::allocated_dependencies(module.materialized->region,module.materialized->allocation,overlaps);if(!hazards)return Result<Module>::err(stage("spill",hazards.error()));module.materialized->region=std::move(hazards.value());
       if(options.schedule){auto scheduled=schedrow::schedule(module.materialized->region,target.scheduling);if(!scheduled)return Result<Module>::err(stage("spill-schedule",scheduled.error()));auto verified=schedrow::verify(module.materialized->region,target.scheduling,scheduled.value());if(!verified)return Result<Module>::err(stage("spill-schedule",verified.error()));module.materialized->scheduled=std::move(scheduled.value());order.clear();for(auto& s:module.materialized->scheduled)order.push_back(s.id);module.materialized->order=order;module.order=order;}
        module.stages.push_back("spill");
       }else if(module.allocation->spilled.empty()) {
         active_stage="allocated-schedule";
        // Physical hazards must follow the order used to form the allocation
        // problem, which may differ from the selector's original layout.
        auto layout=module.selected;layout.instructions.clear();
        for(auto id:order)layout.instructions.push_back(*std::find_if(module.selected.instructions.begin(),module.selected.instructions.end(),[&](auto& i){return i.id==id;}));
         auto overlaps=regtl::register_aliases(module.allocation_problem);auto hazards=regtl::allocated_dependencies(layout,*module.allocation,overlaps);if(!hazards)return Result<Module>::err(stage("allocated-schedule",hazards.error()));module.selected=std::move(hazards.value());
        if(options.schedule){auto final=schedrow::schedule(module.selected,target.scheduling);if(!final)return Result<Module>::err(stage("allocated-schedule",final.error()));auto checked=schedrow::verify(module.selected,target.scheduling,final.value());if(!checked)return Result<Module>::err(stage("allocated-schedule",checked.error()));module.scheduled=std::move(final.value());order.clear();for(auto& s:module.scheduled)order.push_back(s.id);module.order=order;module.stages.push_back("allocated-schedule");}
      }
  }
    active_stage="machine-ir";
    module.machine_ir=emit(module,order);
   machineir_bridge::RegionExchange exchange;exchange.module=module.name;exchange.target=module.target;exchange.region=module.materialized?module.materialized->region:module.selected;exchange.order=order;exchange.schedule=module.materialized?module.materialized->scheduled:module.scheduled;exchange.outputs=program.outputs;exchange.allocation=module.materialized?std::optional<regtl::Allocation>(module.materialized->allocation):module.allocation;
   if(module.materialized){exchange.spill_slots=module.materialized->slots;exchange.frame_size=module.materialized->frame_size;}
   std::set<uint32_t> materialized(program.outputs.begin(),program.outputs.end());for(auto& i:exchange.region.instructions){materialized.insert(i.defs.begin(),i.defs.end());materialized.insert(i.uses.begin(),i.uses.end());}
   for(auto& b:exchange.region.blocks)materialized.insert(b.live_out.begin(),b.live_out.end());for(auto& slot:exchange.spill_slots)materialized.insert(slot.value);
   for(auto& node:program.nodes)if(materialized.contains(node.id)){auto klass=node.register_class;if(options.allocate){auto range=std::find_if(module.allocation_problem.ranges.begin(),module.allocation_problem.ranges.end(),[&](auto& r){return r.value==node.id;});if(range==module.allocation_problem.ranges.end())return Result<Module>::err({Error::Code::Conflict,"machine-ir: missing allocated value"});klass=range->klass;}exchange.values.push_back({node.id,node.type,klass});}
   if(module.materialized)for(auto [value,source]:module.materialized->value_sources){auto node=std::find_if(program.nodes.begin(),program.nodes.end(),[&](auto& n){return n.id==source;});auto range=std::find_if(module.allocation_problem.ranges.begin(),module.allocation_problem.ranges.end(),[&](auto& r){return r.value==source;});exchange.values.push_back({value,node->type,range->klass});}
   auto handoff=machineir_bridge::serialize(exchange);if(!handoff)return Result<Module>::err(stage("machine-ir",handoff.error()));module.machine_ir_exchange=std::move(handoff.value());module.stages.push_back("machine-ir");
    if(options.encode) {
      active_stage="encode";
     if(!target.backend)return Result<Module>::err({Error::Code::Unsupported,"encode: target has no backend adapter"});
     auto encoded=target.backend(module);if(!encoded)return Result<Module>::err(stage("encode",encoded.error()));
     for(auto& relocation:encoded.value().relocations)if(relocation.offset>=encoded.value().bytes.size()||relocation.kind.empty()||relocation.symbol.empty())return Result<Module>::err({Error::Code::Conflict,"encode: malformed backend relocation"});
     module.encoded=std::move(encoded.value());module.stages.push_back("encode");
   }
    return Result<Module>::ok(std::move(module));
  }catch(const Error& error){return Result<Module>::err(stage(active_stage,error));}
  catch(const std::bad_alloc&){return Result<Module>::err(stage(active_stage,{Error::Code::ResourceLimit,"allocation failed"}));}
  catch(const std::exception& error){return Result<Module>::err(stage(active_stage,{Error::Code::Internal,error.what()}));}
  catch(...){return Result<Module>::err(stage(active_stage,{Error::Code::Internal,"pipeline adapter exception"}));}
}
Result<Module> run_pipeline(std::string_view input,const PipelineOptions& options) {
  auto compiled=traceml::compile(input);if(!compiled)return Result<Module>::err(stage("frontend",compiled.error()));
  traceml::ExecutionOptions execution_options;execution_options.record_trace=options.trace_execution;
  auto execution=traceml::execute(compiled.value(),execution_options);if(!execution)return Result<Module>::err(stage("frontend",execution.error()));
  // Portable IR pseudo-instruction selection belongs to the frontend adapter.
  auto lowering=options.trace_execution?traceml::lower_trace(execution.value()):Result<traceml::PortableLowering>::ok(traceml::lower_graph(execution.value().value));if(!lowering)return Result<Module>::err(stage("trace-lambda",lowering.error()));
  auto lowered=std::move(lowering.value());PipelineTarget target;target.name="portable-machineir";
  target.patterns=std::move(lowered.patterns);
  for(auto& i:lowered.instruction_models){InstructionModel model{i.latency,i.throughput,i.resources,i.opcode_class,i.semantic_class,i.barrier,i.memory};model.terminator=i.terminator;target.instructions.emplace(i.opcode,std::move(model));}
  auto result=run_pipeline(lowered.program,target,options);
  if(result){result.value().stages.insert(result.value().stages.begin(),{"frontend","trace-lambda"});if(options.trace_execution)result.value().execution=std::move(execution.value());}
  return result;
}
Result<Module> run_pipeline(std::string_view input,const PipelineTarget& target,const PipelineOptions& options) {
  auto compiled=traceml::compile(input);if(!compiled)return Result<Module>::err(stage("frontend",compiled.error()));
  traceml::ExecutionOptions execution_options;execution_options.record_trace=options.trace_execution;
  auto execution=traceml::execute(compiled.value(),execution_options);if(!execution)return Result<Module>::err(stage("frontend",execution.error()));
  auto lowering=options.trace_execution?traceml::lower_trace(execution.value()):Result<traceml::PortableLowering>::ok(traceml::lower_graph(execution.value().value));if(!lowering)return Result<Module>::err(stage("trace-lambda",lowering.error()));
  auto result=run_pipeline(lowering.value().program,target,options);
  if(result){result.value().stages.insert(result.value().stages.begin(),{"frontend","trace-lambda"});if(options.trace_execution)result.value().execution=std::move(execution.value());}
  return result;
}
}
