#include "scheduling_adapter.hpp"
#include "schedrow/motion.hpp"
#include <limits>
#include <set>

namespace limestone::regtl {
Result<schedrow::Region> allocated_dependencies(const schedrow::Region& input,const Allocation& allocation,std::span<const std::pair<PReg,PReg>> aliases) {
  auto shadow=input;
  for(auto& i:shadow.instructions){
    for(auto v:i.uses){if(!allocation.regs.contains(v))return Result<schedrow::Region>::err({Error::Code::Conflict,"missing post-allocation input"});i.implicit_uses.push_back(allocation.regs.at(v));}
    auto operand_timings=i.operand_latencies;
    for(auto v:i.defs){
      if(!allocation.regs.contains(v))return Result<schedrow::Region>::err({Error::Code::Conflict,"missing post-allocation definition"});auto physical=allocation.regs.at(v);auto range=schedrow::latency_bounds(i,v,false);bool defined=std::find(i.implicit_defs.begin(),i.implicit_defs.end(),physical)!=i.implicit_defs.end();
      if(defined){auto architectural=schedrow::latency_bounds(i,physical,true);range={std::max(range.minimum,architectural.minimum),std::max(range.maximum,architectural.maximum)};}
      for(auto& timing:operand_timings)if(!timing.implicit&&timing.result==v)for(auto& consumer:input.instructions)if(consumer.opcode==timing.consumer_opcode&&timing.use_index<consumer.uses.size()&&consumer.uses[timing.use_index]==v){
        auto index=uint64_t(consumer.implicit_uses.size())+timing.use_index;if(index>UINT32_MAX)return Result<schedrow::Region>::err({Error::Code::ResourceLimit,"post-allocation operand index overflow"});auto cycles=timing.cycles;
        if(defined){auto architectural=schedrow::latency_bounds(i,physical,true,&consumer,uint32_t(index));cycles={std::max(cycles.minimum,architectural.minimum),std::max(cycles.maximum,architectural.maximum)};}
        auto found=std::find_if(i.operand_latencies.begin(),i.operand_latencies.end(),[&](auto& t){return t.implicit&&t.result==physical&&t.consumer_opcode==consumer.opcode&&t.use_index==index;});
        if(found==i.operand_latencies.end())i.operand_latencies.push_back({physical,consumer.opcode,uint32_t(index),cycles,true});else found->cycles={std::max(found->cycles.minimum,cycles.minimum),std::max(found->cycles.maximum,cycles.maximum)};
      }
      i.implicit_result_latency.erase(physical);i.implicit_result_latency_ranges[physical]=range;if(!defined)i.implicit_defs.push_back(physical);
    }
  }
  auto augmented=schedrow::dependencies(shadow,aliases);if(!augmented)return augmented;auto result=input;
  for(auto d:augmented.value().deps)if(std::none_of(input.deps.begin(),input.deps.end(),[&](auto& e){return e.producer==d.producer&&e.consumer==d.consumer&&e.kind==d.kind&&e.latency>=d.latency&&e.distance==d.distance;})){d.scheduler_only=true;result.deps.push_back(d);}
  return Result<schedrow::Region>::ok(std::move(result));
}
namespace {
bool aliases(const Program& p,PReg a,PReg b) {
  return registers_overlap(p,a,b);
}
Result<int> validate_spills(const Program& p,std::span<const SpillClass> classes) {
  std::set<std::string> names;std::set<PReg> physical;for(auto& c:p.classes)physical.insert(c.members.begin(),c.members.end());
  for(auto& c:classes){auto klass=std::find_if(p.classes.begin(),p.classes.end(),[&](auto& k){return k.name==c.klass;});if(!names.insert(c.klass).second||klass==p.classes.end()||c.load_opcode.empty()||c.store_opcode.empty()||c.address_space.empty()||!c.size||!c.alignment||(c.alignment&(c.alignment-1))||c.scratch.empty())return Result<int>::err({Error::Code::InvalidArgument,"invalid spill class contract: "+c.klass});std::set<PReg> seen;for(auto r:c.scratch)if(!seen.insert(r).second||std::find(klass->members.begin(),klass->members.end(),r)==klass->members.end())return Result<int>::err({Error::Code::InvalidArgument,"invalid spill scratch register"});}
  return Result<int>::ok(0);
}
Result<std::map<VReg,const schedrow::Instruction*>> recipe_map(const schedrow::Region& input,std::span<const VReg> values) {
  using Output=Result<std::map<VReg,const schedrow::Instruction*>>;std::map<VReg,const schedrow::Instruction*> definitions,result;
  for(auto& instruction:input.instructions)for(auto value:instruction.defs)if(!definitions.emplace(value,&instruction).second)return Output::err({Error::Code::Conflict,"rematerialization requires unique SSA definitions"});
  for(auto value:values) {
    if(!definitions.contains(value)||result.contains(value))return Output::err({Error::Code::InvalidArgument,"unknown or duplicate rematerialization value"});auto recipe=definitions.at(value);
    if(recipe->defs!=std::vector<VReg>{value}||!recipe->implicit_defs.empty()||!recipe->implicit_uses.empty()||recipe->memory||recipe->access||recipe->barrier||recipe->call||recipe->terminator||recipe->may_trap||!recipe->speculative||recipe->control!=schedrow::ControlFlow::None||!recipe->ties.empty()||!recipe->early_defs.empty())return Output::err({Error::Code::Unsupported,"rematerialization requires a pure unconstrained SSA definition"});
    result[value]=recipe;
  }
  return Output::ok(std::move(result));
}
}
Result<Program> prepare_rematerialization(const schedrow::Region& input,std::span<const uint32_t> order,const Program& problem,std::span<const VReg> values,std::span<const VReg> outputs,size_t work_limit) {
  using Output=Result<Program>;auto valid=validate(problem);if(!valid)return Output::err(valid.error());auto overlaps=register_aliases(problem);valid=schedrow::verify_order(input,order,overlaps);if(!valid)return Output::err(valid.error());
  auto recipes=recipe_map(input,values);if(!recipes)return Output::err(recipes.error());if(values.empty())return Output::ok(problem);
  Function function;function.classes=problem.classes;function.aliases=problem.aliases;function.storage=problem.storage;function.tuples=problem.tuples;function.reserved=problem.reserved;
  std::map<VReg,const LiveRange*> ranges;for(auto& range:problem.ranges){ranges[range.value]=&range;function.values.push_back({range.value,range.klass,range.constraint,range.spillable});}
  for(auto value:values)if(!ranges.contains(value))return Output::err({Error::Code::InvalidArgument,"recipe has no allocation range"});
  std::map<uint32_t,Block> blocks;std::map<uint32_t,const schedrow::Instruction*> instructions;
  if(input.blocks.empty())blocks[0]={0,{}, {},std::vector<VReg>(outputs.begin(),outputs.end())};else for(auto& block:input.blocks){blocks[block.id]={block.id,{},block.successors,block.live_out};if(block.successors.empty())blocks[block.id].live_out.insert(blocks[block.id].live_out.end(),outputs.begin(),outputs.end());}
  for(auto& instruction:input.instructions)instructions[instruction.id]=&instruction;
  for(auto id:order) {
    auto& i=*instructions.at(id);Instruction lowered{i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses};std::set<VReg> visited;std::vector<VReg> work=i.uses;
    while(!work.empty()){if(!work_limit--)return Output::err({Error::Code::ResourceLimit,"rematerialization liveness work limit"});auto value=work.back();work.pop_back();if(!visited.insert(value).second)continue;if(!recipes.value().contains(value))continue;for(auto operand:recipes.value().at(value)->uses){lowered.uses.push_back(operand);work.push_back(operand);}}
    std::sort(lowered.uses.begin(),lowered.uses.end());lowered.uses.erase(std::unique(lowered.uses.begin(),lowered.uses.end()),lowered.uses.end());blocks.at(input.blocks.empty()?0:i.block).instructions.push_back(std::move(lowered));
  }
  for(auto& [id,block]:blocks)function.blocks.push_back(std::move(block));auto analysis=analyze(function);if(!analysis)return Output::err(analysis.error());auto result=problem;
  // Preserve custom interference/cost/ABI constraints, adding the recipe-derived
  // lifetime and interference facts to the original problem transactionally.
  for(auto& range:analysis.value().problem.ranges){auto& existing=*std::find_if(result.ranges.begin(),result.ranges.end(),[&](auto& r){return r.value==range.value;});existing.begin=std::min(existing.begin,range.begin);existing.end=std::max(existing.end,range.end);existing.constraint.forbidden.insert(existing.constraint.forbidden.end(),range.constraint.forbidden.begin(),range.constraint.forbidden.end());std::sort(existing.constraint.forbidden.begin(),existing.constraint.forbidden.end());existing.constraint.forbidden.erase(std::unique(existing.constraint.forbidden.begin(),existing.constraint.forbidden.end()),existing.constraint.forbidden.end());}
  result.interference.insert(result.interference.end(),analysis.value().problem.interference.begin(),analysis.value().problem.interference.end());std::sort(result.interference.begin(),result.interference.end());result.interference.erase(std::unique(result.interference.begin(),result.interference.end()),result.interference.end());valid=validate(result);if(!valid)return Output::err(valid.error());return Output::ok(std::move(result));
}
Result<Program> reserve_spill_registers(const Program& input,std::span<const SpillClass> classes) {
  auto checked=validate(input);if(!checked)return Result<Program>::err(checked.error());checked=validate_spills(input,classes);if(!checked)return Result<Program>::err(checked.error());auto result=input;
  for(auto& c:classes)result.reserved.insert(result.reserved.end(),c.scratch.begin(),c.scratch.end());std::sort(result.reserved.begin(),result.reserved.end());result.reserved.erase(std::unique(result.reserved.begin(),result.reserved.end()),result.reserved.end());
  checked=validate(result);if(!checked)return Result<Program>::err(checked.error());return Result<Program>::ok(std::move(result));
}
Result<AllocatedRegion> materialize_spills(const schedrow::Region& input,std::span<const uint32_t> order,const Program& problem,const Allocation& assignment,std::span<const SpillClass> classes,std::span<const VReg> outputs,std::span<const VReg> rematerializable,const SpillOptions& options) {
  using Output=Result<AllocatedRegion>;
  size_t remaining=options.work_limit;
  auto spend=[&]()->bool{if(!remaining)return false;--remaining;return true;};
  auto prepared=prepare_rematerialization(input,order,problem,rematerializable,outputs);if(!prepared)return Output::err(prepared.error());auto checked=verify(prepared.value(),assignment);if(!checked)return Output::err(checked.error());checked=validate_spills(problem,classes);if(!checked)return Output::err(checked.error());
  auto overlaps=register_aliases(problem);checked=schedrow::verify_order(input,order,overlaps);if(!checked)return Output::err(checked.error());
  std::map<VReg,const LiveRange*> ranges;std::map<std::string,const SpillClass*> models;std::map<uint32_t,const schedrow::Instruction*> instructions;uint64_t next_value=0,next_instruction=0;
  for(auto& r:problem.ranges){ranges[r.value]=&r;next_value=std::max(next_value,uint64_t(r.value)+1);}for(auto& c:classes)models[c.klass]=&c;
  for(auto& i:input.instructions){if(!instructions.emplace(i.id,&i).second)return Output::err({Error::Code::Conflict,"duplicate spill input instruction"});next_instruction=std::max(next_instruction,uint64_t(i.id)+1);}
  if(order.size()!=instructions.size())return Output::err({Error::Code::Conflict,"incomplete spill input order"});std::set<uint32_t> seen;for(auto id:order)if(!instructions.contains(id)||!seen.insert(id).second)return Output::err({Error::Code::Conflict,"invalid spill input order"});
  Function source;source.classes=problem.classes;source.aliases=problem.aliases;
  source.storage=problem.storage;source.tuples=problem.tuples;
  for(auto& range:problem.ranges)source.values.push_back({range.value,range.klass,range.constraint,range.spillable});
  std::map<uint32_t,Block> source_blocks;
  if(input.blocks.empty())source_blocks[0]={0};else for(auto& b:input.blocks)source_blocks[b.id]={b.id,{},b.successors,b.live_out};
  for(auto id:order){auto& i=*instructions.at(id);source_blocks.at(input.blocks.empty()?0:i.block).instructions.push_back({i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses});}
  for(auto& [id,block]:source_blocks)source.blocks.push_back(std::move(block));auto physical_state=analyze(source);if(!physical_state)return Output::err(physical_state.error());
  std::set<VReg> spilled(assignment.spilled.begin(),assignment.spilled.end()),defined;
  auto checked_recipes=recipe_map(input,rematerializable);if(!checked_recipes)return Output::err(checked_recipes.error());auto recipes=std::move(checked_recipes.value());
  for(auto& i:input.instructions)defined.insert(i.defs.begin(),i.defs.end());
  for(auto v:spilled)if(!defined.contains(v))return Output::err({Error::Code::Unsupported,"spilled live-in needs a calling-convention transfer"});
  for(auto v:outputs)if(spilled.contains(v))return Output::err({Error::Code::Unsupported,"spilled output needs a calling-convention transfer"});
  for(auto& b:input.blocks)for(auto v:b.live_out)if(spilled.contains(v))return Output::err({Error::Code::Unsupported,"spilled explicit live-out needs a boundary transfer"});
  for(auto [value,physical]:assignment.regs)for(auto& c:classes)for(auto scratch:c.scratch)if(aliases(problem,physical,scratch))return Output::err({Error::Code::Conflict,"spill scratch overlaps allocated storage"});
  AllocatedRegion result;result.region=input;result.region.instructions.clear();result.allocation=assignment;result.allocation.spilled.clear();std::map<VReg,size_t> slots;
  for(auto v:spilled){auto& r=*ranges.at(v);if(!models.contains(r.klass))return Output::err({Error::Code::Unsupported,"missing spill class for v"+std::to_string(v)});if(recipes.contains(v))continue;auto& c=*models.at(r.klass);auto padding=(c.alignment-result.frame_size%c.alignment)%c.alignment;if(padding>UINT64_MAX-result.frame_size||c.size>UINT64_MAX-result.frame_size-padding)return Output::err({Error::Code::ResourceLimit,"spill frame overflow"});result.frame_size+=padding;slots[v]=result.slots.size();result.slots.push_back({v,r.klass,result.frame_size,c.size,c.alignment});result.frame_size+=c.size;}
  auto identity=[&](uint64_t& next)->Result<uint32_t>{if(next>UINT32_MAX)return Result<uint32_t>::err({Error::Code::ResourceLimit,"spill temporary identity overflow"});return Result<uint32_t>::ok(static_cast<uint32_t>(next++));};
  std::map<uint32_t,size_t> positions;for(size_t k=0;k<order.size();++k)positions[order[k]]=k;
  std::vector<std::pair<size_t,size_t>> intervals,units;
  for(auto& group:input.groups)if(group.kind!=schedrow::GroupKind::Ordered)intervals.emplace_back(positions.at(group.members.front()),positions.at(group.members.back()));
  std::sort(intervals.begin(),intervals.end());for(auto interval:intervals){if(!units.empty()&&interval.first<=units.back().second)units.back().second=std::max(units.back().second,interval.second);else units.push_back(interval);}
  std::map<size_t,size_t> boundaries(units.begin(),units.end());
  std::map<VReg,std::set<uint32_t>> consumers;for(auto& i:input.instructions){std::vector<VReg> work=i.uses;std::set<VReg> visited;while(!work.empty()){if(!spend())return Output::err({Error::Code::ResourceLimit,"spill materialization work limit"});auto value=work.back();work.pop_back();if(!visited.insert(value).second)continue;if(spilled.contains(value))consumers[value].insert(i.id);if(spilled.contains(value)&&recipes.contains(value))for(auto operand:recipes.at(value)->uses)work.push_back(operand);}}
  for(size_t position=0;position<order.size();) {
    if(!spend())return Output::err({Error::Code::ResourceLimit,"spill materialization work limit"});
    auto end=boundaries.contains(position)?boundaries.at(position)+1:position+1;
    std::vector<uint32_t> unit(order.begin()+position,order.begin()+end);position=end;std::set<uint32_t> unit_ids(unit.begin(),unit.end());
    std::vector<schedrow::Instruction> operations;std::map<VReg,VReg> temporary;
    std::set<VReg> involved,unit_defs,reloads,stores;
    for(auto id:unit){auto& i=*instructions.at(id);operations.push_back(i);involved.insert(i.uses.begin(),i.uses.end());involved.insert(i.defs.begin(),i.defs.end());for(auto v:i.uses)if(spilled.contains(v)&&!unit_defs.contains(v))reloads.insert(v);unit_defs.insert(i.defs.begin(),i.defs.end());}
    std::vector<VReg> recipe_order;std::set<VReg> visiting,ready;std::vector<std::pair<VReg,bool>> work;for(auto value:reloads)work.emplace_back(value,false);
    while(!work.empty()) {
      if(!spend())return Output::err({Error::Code::ResourceLimit,"spill materialization work limit"});
      auto [value,finish]=work.back();work.pop_back();if(ready.contains(value))continue;
      if(finish){visiting.erase(value);ready.insert(value);recipe_order.push_back(value);continue;}
      if(!visiting.insert(value).second)return Output::err({Error::Code::Conflict,"cyclic rematerialization recipe"});
      if(!recipes.contains(value)){visiting.erase(value);ready.insert(value);continue;}
      work.emplace_back(value,true);
      for(auto operand:recipes.at(value)->uses){involved.insert(operand);if(spilled.contains(operand)){reloads.insert(operand);work.emplace_back(operand,false);}}
    }
    for(auto v:unit_defs)if(spilled.contains(v)&&!recipes.contains(v)&&std::any_of(consumers[v].begin(),consumers[v].end(),[&](auto id){return !unit_ids.contains(id);}))stores.insert(v);
    std::vector<schedrow::Instruction> before,after;
    std::map<VReg,std::vector<Constraint>> transfer_constraints;
    auto transfer=[&](VReg source,bool load)->Result<schedrow::Instruction>{
      auto fresh=identity(next_instruction);if(!fresh)return Result<schedrow::Instruction>::err(fresh.error());auto& slot=result.slots[slots.at(source)];if(slot.offset>INT64_MAX)return Result<schedrow::Instruction>::err({Error::Code::ResourceLimit,"spill offset exceeds immediate range"});auto& c=*models.at(slot.klass);schedrow::Instruction t{};t.id=fresh.value();t.block=operations.front().block;t.opcode=load?c.load_opcode:c.store_opcode;t.register_classes[source]=slot.klass;if(load)t.defs={source};else t.uses={source};t.immediates={{source,static_cast<int64_t>(slot.offset)}};t.access=schedrow::MemoryAccess{load,!load,false,false,schedrow::MemoryOrdering::Relaxed,c.address_space,{},c.size,c.alignment};t.memory=true;t.speculative=false;t.origin=(load?"reload v":"spill v")+std::to_string(source);
      if(options.configure_transfer){auto configured=options.configure_transfer(t);if(!configured)return Result<schedrow::Instruction>::err(configured.error());
        if(t.id!=fresh.value()||t.block!=operations.front().block||t.defs!=(load?std::vector<VReg>{source}:std::vector<VReg>{})||(!load&&std::find(t.uses.begin(),t.uses.end(),source)==t.uses.end())||t.opcode.empty()||t.terminator||t.call||t.control!=schedrow::ControlFlow::None||!t.memory||!t.access)return Result<schedrow::Instruction>::err({Error::Code::Conflict,"transfer adapter changed its frame/control/value contract"});
        auto& a=*t.access;if(a.read!=load||a.write==load||a.volatile_access||a.atomic||a.ordering!=schedrow::MemoryOrdering::Relaxed||a.address_space!=c.address_space||a.size!=c.size||a.alignment!=c.alignment||!a.alias_sets.empty()||t.immediates!=std::vector<std::pair<VReg,int64_t>>{{source,static_cast<int64_t>(slot.offset)}})return Result<schedrow::Instruction>::err({Error::Code::Conflict,"transfer adapter changed private-frame addressing"});
        for(auto& [value,constraint]:configured.value()){if(std::find(t.defs.begin(),t.defs.end(),value)==t.defs.end()&&std::find(t.uses.begin(),t.uses.end(),value)==t.uses.end())return Result<schedrow::Instruction>::err({Error::Code::InvalidArgument,"transfer constraint has no operand"});transfer_constraints[value].push_back(std::move(constraint));}
      }
      for(auto operand:t.uses){if(!ranges.contains(operand))return Result<schedrow::Instruction>::err({Error::Code::NotFound,"unknown transfer operand"});if(spilled.contains(operand)&&operand!=source)return Result<schedrow::Instruction>::err({Error::Code::Unsupported,"additional spilled transfer operand needs a reconstruction adapter"});involved.insert(operand);t.register_classes[operand]=ranges.at(operand)->klass;}
      return Result<schedrow::Instruction>::ok(std::move(t));
    };
    for(auto value:reloads)if(!recipes.contains(value)){auto t=transfer(value,true);if(!t)return Output::err(t.error());before.push_back(std::move(t.value()));}
    for(auto value:stores){auto t=transfer(value,false);if(!t)return Output::err(t.error());after.push_back(std::move(t.value()));}
    for(auto v:involved)if(!ranges.contains(v))return Output::err({Error::Code::Conflict,"spill instruction references unknown value"});
    for(auto v:involved)if(spilled.contains(v)){auto fresh=identity(next_value);if(!fresh)return Output::err(fresh.error());temporary[v]=fresh.value();result.value_sources[fresh.value()]=v;}
    if(!temporary.empty()) {
      Function local;local.classes=problem.classes;local.aliases=problem.aliases;Block block{0};block.live_out.assign(stores.begin(),stores.end());
      local.storage=problem.storage;
      for(auto& i:before)block.instructions.push_back({i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses});
      for(auto value:recipe_order){auto fresh=identity(next_instruction);if(!fresh)return Output::err(fresh.error());block.instructions.push_back({fresh.value(),{value},recipes.at(value)->uses});}
      for(auto& i:operations)block.instructions.push_back({i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses});
      for(auto& i:after)block.instructions.push_back({i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses});
      for(auto v:involved){auto& r=*ranges.at(v);Constraint constraint;if(temporary.contains(v)){constraint=r.constraint;constraint.allowed=models.at(r.klass)->scratch;
          for(auto reg:constraint.allowed)if(!r.constraint.allowed.empty()&&std::find(r.constraint.allowed.begin(),r.constraint.allowed.end(),reg)==r.constraint.allowed.end())constraint.forbidden.push_back(reg);
          // Unit temporaries survive until their boundary transfers. Protect all
          // architectural state visible inside the unit, including aliased views.
          for(auto reg:constraint.allowed)for(auto id:unit)for(auto& state:{physical_state.value().physical_before.at(id),physical_state.value().physical_after.at(id)})for(auto live:state)if(aliases(problem,reg,live))constraint.forbidden.push_back(reg);
        }else constraint.fixed=assignment.regs.at(v);
        for(auto& extra:transfer_constraints[v]){
          if(extra.fixed){if(constraint.fixed&&constraint.fixed!=extra.fixed)return Output::err({Error::Code::Conflict,"conflicting transfer fixed-register constraints"});constraint.fixed=extra.fixed;}
          if(!extra.bank.empty()){if(!constraint.bank.empty()&&constraint.bank!=extra.bank)return Output::err({Error::Code::Conflict,"conflicting transfer register banks"});constraint.bank=extra.bank;}
          constraint.forbidden.insert(constraint.forbidden.end(),extra.forbidden.begin(),extra.forbidden.end());
          if(!extra.allowed.empty()){if(constraint.allowed.empty())constraint.allowed=extra.allowed;else{std::vector<PReg> intersection;for(auto reg:constraint.allowed)if(std::find(extra.allowed.begin(),extra.allowed.end(),reg)!=extra.allowed.end())intersection.push_back(reg);if(intersection.empty())return Output::err({Error::Code::Unsatisfiable,"disjoint transfer allowed registers"});constraint.allowed=std::move(intersection);}}
        }
        local.values.push_back({v,r.klass,std::move(constraint),false});}
      local.blocks.push_back(std::move(block));auto analysis=analyze(local);if(!analysis)return Output::err(analysis.error());auto allocation=constraint_allocate(analysis.value().problem);if(!allocation)return Output::err(allocation.error());for(auto [v,tmp]:temporary)result.allocation.regs[tmp]=allocation.value().regs.at(v);
      auto replace=[&](VReg v){return temporary.contains(v)?temporary.at(v):v;};
      for(auto& i:operations){for(auto& v:i.uses)v=replace(v);for(auto& v:i.defs)v=replace(v);for(auto& v:i.early_defs)v=replace(v);for(auto& [a,b]:i.ties){a=replace(a);b=replace(b);}std::unordered_map<VReg,std::string> renamed;for(auto& [v,klass]:i.register_classes)renamed[replace(v)]=klass;i.register_classes=std::move(renamed);std::unordered_map<VReg,uint32_t> latencies;for(auto [v,latency]:i.result_latency)latencies[replace(v)]=latency;i.result_latency=std::move(latencies);std::unordered_map<VReg,schedrow::LatencyRange> bounds;for(auto [v,range]:i.result_latency_ranges)bounds[replace(v)]=range;i.result_latency_ranges=std::move(bounds);for(auto& timing:i.operand_latencies)if(!timing.implicit)timing.result=replace(timing.result);}
      for(auto* transfers:{&before,&after})for(auto& i:*transfers){for(auto& v:i.uses)v=replace(v);for(auto& v:i.defs)v=replace(v);for(auto& v:i.early_defs)v=replace(v);for(auto& [a,b]:i.ties){a=replace(a);b=replace(b);}std::unordered_map<VReg,std::string> renamed;for(auto& [v,klass]:i.register_classes)renamed[replace(v)]=klass;i.register_classes=std::move(renamed);std::unordered_map<VReg,uint32_t> latency;for(auto [v,cycles]:i.result_latency)latency[replace(v)]=cycles;i.result_latency=std::move(latency);std::unordered_map<VReg,schedrow::LatencyRange> bounds;for(auto [v,range]:i.result_latency_ranges)bounds[replace(v)]=range;i.result_latency_ranges=std::move(bounds);for(auto& timing:i.operand_latencies)if(!timing.implicit)timing.result=replace(timing.result);}
    }
    result.region.instructions.insert(result.region.instructions.end(),before.begin(),before.end());
    for(auto v:recipe_order){auto fresh=identity(next_instruction);if(!fresh)return Output::err(fresh.error());auto t=*recipes.at(v);t.id=fresh.value();t.block=operations.front().block;t.defs={temporary.at(v)};for(auto& operand:t.uses)if(temporary.contains(operand))operand=temporary.at(operand);t.register_classes.clear();for(auto operand:t.uses)t.register_classes[operand]=ranges.at(result.value_sources.contains(operand)?result.value_sources.at(operand):operand)->klass;t.register_classes[temporary.at(v)]=ranges.at(v)->klass;auto latency=schedrow::latency_bounds(t,v,false);t.result_latency.clear();t.result_latency_ranges.clear();t.result_latency_ranges[temporary.at(v)]=latency;for(auto& timing:t.operand_latencies)if(!timing.implicit)timing.result=temporary.at(v);t.origin="rematerialize v"+std::to_string(v)+" "+t.origin;result.region.instructions.push_back(std::move(t));}
    if(!stores.empty()&&operations.back().terminator)return Output::err({Error::Code::Unsupported,"spilled terminating unit needs a control-flow transfer adapter"});
    result.region.instructions.insert(result.region.instructions.end(),operations.begin(),operations.end());
    result.region.instructions.insert(result.region.instructions.end(),after.begin(),after.end());
  }
  std::map<uint32_t,uint32_t> prior;
  for(auto& i:result.region.instructions){result.order.push_back(i.id);if(prior.contains(i.block))result.region.deps.push_back({prior.at(i.block),i.id,schedrow::DepKind::Ordering,0,0,true});prior[i.block]=i.id;}
  checked=schedrow::verify_order(result.region,result.order,overlaps);if(!checked)return Output::err(checked.error());
  Function function;function.classes=problem.classes;function.aliases=problem.aliases;std::map<uint32_t,Block> blocks;
  function.storage=problem.storage;function.tuples=problem.tuples;
  if(input.blocks.empty())blocks[0]={0,{}, {},std::vector<VReg>(outputs.begin(),outputs.end())};else for(auto& b:input.blocks){blocks[b.id]={b.id,{},b.successors,b.live_out};if(b.successors.empty())blocks[b.id].live_out.insert(blocks[b.id].live_out.end(),outputs.begin(),outputs.end());}
  for(auto& i:result.region.instructions)blocks.at(input.blocks.empty()?0:i.block).instructions.push_back({i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses});
  for(auto& [id,b]:blocks)function.blocks.push_back(std::move(b));
  for(auto [value,physical]:result.allocation.regs){auto source=result.value_sources.contains(value)?result.value_sources.at(value):value;function.values.push_back({value,ranges.at(source)->klass,{{},{},physical},false});}
  auto analysis=analyze(function);if(!analysis)return Output::err(analysis.error());result.problem=std::move(analysis.value().problem);checked=verify(result.problem,result.allocation);if(!checked)return Output::err(checked.error());return Output::ok(std::move(result));
}
Result<AllocatedRegion> materialize_spills(const schedrow::Region& input,std::span<const uint32_t> order,const Program& problem,const Allocation& assignment,std::span<const SpillClass> classes,std::span<const VReg> outputs,const SpillAdapter& adapter) {
  using Output=Result<AllocatedRegion>;
  try {
    if(!adapter.lower||!adapter.prove)return Output::err({Error::Code::InvalidArgument,"spill reconstruction requires lowering and semantic proof"});
    auto valid=verify(problem,assignment);if(!valid)return Output::err(valid.error());valid=schedrow::verify_order(input,order,register_aliases(problem));if(!valid)return Output::err(valid.error());
    auto lowered=adapter.lower(input,order,problem,assignment,classes,outputs);if(!lowered)return lowered;auto& result=lowered.value();
    if(!result.allocation.spilled.empty()||!result.scheduled.empty())return Output::err({Error::Code::Conflict,"spill reconstruction must produce fully allocated unscheduled code"});
    valid=verify(result.problem,result.allocation);if(!valid)return Output::err(valid.error());valid=schedrow::verify_order(result.region,result.order,register_aliases(result.problem));if(!valid)return Output::err(valid.error());
    std::set<VReg> slot_values;std::vector<std::pair<uint64_t,uint64_t>> extents;
    for(auto& slot:result.slots){if(!slot_values.insert(slot.value).second||!slot.size||!slot.alignment||(slot.alignment&(slot.alignment-1))||slot.offset%slot.alignment||slot.offset>result.frame_size||slot.size>result.frame_size-slot.offset)return Output::err({Error::Code::Conflict,"invalid reconstructed spill frame"});extents.emplace_back(slot.offset,slot.offset+slot.size);}
    std::sort(extents.begin(),extents.end());for(size_t k=1;k<extents.size();++k)if(extents[k].first<extents[k-1].second)return Output::err({Error::Code::Conflict,"overlapping reconstructed spill slots"});
    std::map<VReg,const LiveRange*> ranges;for(auto& range:result.problem.ranges)ranges[range.value]=&range;
    for(auto& instruction:result.region.instructions)for(auto& operands:{instruction.defs,instruction.uses})for(auto value:operands)if(!ranges.contains(value)||!result.allocation.regs.contains(value))return Output::err({Error::Code::Conflict,"unallocated reconstructed operand"});
    auto layout=result.region;layout.instructions.clear();for(auto id:result.order)layout.instructions.push_back(*std::find_if(result.region.instructions.begin(),result.region.instructions.end(),[&](auto& i){return i.id==id;}));
    if(layout.blocks.empty()){layout.blocks={{0,"entry"}};layout.entry=0;for(auto& i:layout.instructions)i.block=0;}
    for(auto& block:layout.blocks)if(block.successors.empty())block.live_out.insert(block.live_out.end(),outputs.begin(),outputs.end());
    valid=schedrow::verify_ssa(layout,adapter.work_limit);if(!valid)return Output::err(valid.error());
    Function function;function.classes=result.problem.classes;function.aliases=result.problem.aliases;function.storage=result.problem.storage;function.tuples=result.problem.tuples;
    for(auto& range:result.problem.ranges)function.values.push_back({range.value,range.klass,range.constraint,false});
    for(auto& block:layout.blocks){Block lowered_block{block.id,{},block.successors,block.live_out};for(auto& i:layout.instructions)if(i.block==block.id){for(auto& [value,klass]:i.register_classes)if(!ranges.contains(value)||ranges.at(value)->klass!=klass)return Output::err({Error::Code::Conflict,"reconstructed operand class mismatch"});lowered_block.instructions.push_back({i.id,i.defs,i.uses,i.early_defs,i.implicit_defs,i.ties,i.implicit_defs,i.implicit_uses});}function.blocks.push_back(std::move(lowered_block));}
    auto analysis=analyze(function);if(!analysis)return Output::err(analysis.error());valid=verify(analysis.value().problem,result.allocation);if(!valid)return Output::err(valid.error());
    for(auto [value,source]:result.value_sources)if(!ranges.contains(value)||std::none_of(problem.ranges.begin(),problem.ranges.end(),[&](auto& range){return range.value==source&&range.klass==ranges.at(value)->klass;}))return Output::err({Error::Code::Conflict,"invalid reconstructed value provenance"});
    valid=adapter.prove(input,assignment,result);if(!valid)return Output::err(valid.error());return lowered;
  }catch(const Error& error){return Output::err(error);}catch(const std::bad_alloc&){return Output::err({Error::Code::ResourceLimit,"spill reconstruction allocation failed"});}catch(const std::exception& error){return Output::err({Error::Code::Internal,error.what()});}catch(...){return Output::err({Error::Code::Internal,"spill reconstruction exception"});}
}
}
