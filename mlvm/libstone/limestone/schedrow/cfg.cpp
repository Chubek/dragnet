#include "schedrow.hpp"
#include <cmath>
#include <map>
#include <set>

namespace limestone::schedrow {
Result<int> validate_region(const Region& input) {
  using Set=std::set<uint32_t>;
  std::map<uint32_t,const BasicBlock*> blocks;
  for(auto& b:input.blocks)if(!blocks.emplace(b.id,&b).second)return Result<int>::err({Error::Code::InvalidArgument,"duplicate scheduling block"});
  if(!blocks.empty()&&(!blocks.contains(input.entry)||input.blocks.front().id!=input.entry))return Result<int>::err({Error::Code::Conflict,"scheduling entry must lead the block layout"});
  for(auto& b:input.blocks){Set seen;for(auto target:b.successors)if(!blocks.contains(target)||!seen.insert(target).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate scheduling successor"});}
  Set ids;std::map<uint32_t,const Instruction*> last;
  for(auto& i:input.instructions) {
    for(auto& [id,metadata]:i.source_metadata){std::set<uint32_t> indices;for(auto& argument:metadata.strings)if(!indices.insert(argument.index).second||argument.value.find('\0')!=std::string::npos)return Result<int>::err({Error::Code::InvalidArgument,"invalid selected string argument"});}
    if(!ids.insert(i.id).second)return Result<int>::err({Error::Code::InvalidArgument,"duplicate scheduling instruction"});
    if(!i.issue_width)return Result<int>::err({Error::Code::InvalidArgument,"zero instruction issue width"});
    Set allowed_slots(i.issue_slots.begin(),i.issue_slots.end());
    if(allowed_slots.size()!=i.issue_slots.size())return Result<int>::err({Error::Code::InvalidArgument,"duplicate instruction issue slot"});
    if(!i.issue_slots.empty()&&i.issue_width>i.issue_slots.size())return Result<int>::err({Error::Code::Unsatisfiable,"instruction issue width exceeds its legal slot set"});
    if(!std::isfinite(i.throughput)||i.throughput<=0)return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling throughput"});
    for(auto& r:i.resources)if(!r.duration||!std::isfinite(r.quantity)||r.quantity<=0||(r.resource.empty()&&r.alternatives.empty()))return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling resource reservation"});
    for(auto& r:i.resources)for(auto& alternative:r.alternatives)if(alternative.empty())return Result<int>::err({Error::Code::InvalidArgument,"empty scheduling resource alternative"});
    for(auto& [klass,delta]:i.pressure_delta)if(klass.empty())return Result<int>::err({Error::Code::InvalidArgument,"empty register-pressure class"});
    if(i.control<ControlFlow::None||i.control>ControlFlow::Call)return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling control flow"});
    if(i.access&&(i.access->ordering<MemoryOrdering::Relaxed||i.access->ordering>MemoryOrdering::Sequential||(i.access->alignment&&(i.access->alignment&(i.access->alignment-1)))))return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling memory contract"});
    Set defs(i.defs.begin(),i.defs.end());
    if(defs.size()!=i.defs.size())return Result<int>::err({Error::Code::Conflict,"duplicate instruction definition"});
    for(auto value:i.early_defs)if(!defs.contains(value))return Result<int>::err({Error::Code::Conflict,"early definition is not an instruction result"});
    for(auto [a,b]:i.ties)if(!defs.contains(a)||std::find(i.uses.begin(),i.uses.end(),b)==i.uses.end())return Result<int>::err({Error::Code::Conflict,"tie needs an instruction definition and use"});
    for(auto [value,latency]:i.result_latency)if(!defs.contains(value))return Result<int>::err({Error::Code::Conflict,"latency does not name an explicit instruction result"});
    for(auto [value,latency]:i.implicit_result_latency)if(std::find(i.implicit_defs.begin(),i.implicit_defs.end(),value)==i.implicit_defs.end())return Result<int>::err({Error::Code::Conflict,"latency does not name an implicit instruction result"});
    auto bounds=[](LatencyRange range){return range.minimum<=range.maximum;};
    if((i.latency_range&&!bounds(*i.latency_range))||(i.memory_latency&&(!bounds(*i.memory_latency)||(!i.memory&&!i.access))))return Result<int>::err({Error::Code::InvalidArgument,"invalid instruction latency range"});
    for(auto [value,range]:i.result_latency_ranges)if(!defs.contains(value)||!bounds(range)||i.result_latency.contains(value))return Result<int>::err({Error::Code::Conflict,"invalid or duplicate explicit result latency"});
    for(auto [value,range]:i.implicit_result_latency_ranges)if(std::find(i.implicit_defs.begin(),i.implicit_defs.end(),value)==i.implicit_defs.end()||!bounds(range)||i.implicit_result_latency.contains(value))return Result<int>::err({Error::Code::Conflict,"invalid or duplicate implicit result latency"});
    std::set<std::tuple<ValueId,std::string,uint32_t,bool>> timings;
    for(auto& timing:i.operand_latencies){bool defined=timing.implicit?std::find(i.implicit_defs.begin(),i.implicit_defs.end(),timing.result)!=i.implicit_defs.end():defs.contains(timing.result);if(!defined||timing.consumer_opcode.empty()||!bounds(timing.cycles)||!timings.emplace(timing.result,timing.consumer_opcode,timing.use_index,timing.implicit).second)return Result<int>::err({Error::Code::Conflict,"invalid or duplicate operand latency"});}
    for(auto& [value,klass]:i.register_classes)if(klass.empty()||(!defs.contains(value)&&std::find(i.uses.begin(),i.uses.end(),value)==i.uses.end()))return Result<int>::err({Error::Code::Conflict,"register class does not name an instruction operand"});
    bool terminal=i.terminator||(i.control!=ControlFlow::None&&i.control!=ControlFlow::Call);
    if((i.control==ControlFlow::Call&&!i.call)||(i.control!=ControlFlow::None&&i.control!=ControlFlow::Call&&!i.terminator))return Result<int>::err({Error::Code::Conflict,"control flow disagrees with instruction effects"});
    auto block=blocks.empty()?0:i.block;
    if(!blocks.empty()&&!blocks.contains(block))return Result<int>::err({Error::Code::InvalidArgument,"instruction belongs to an unknown scheduling block"});
    if(last.contains(block)&&last.at(block)->terminator)return Result<int>::err({Error::Code::Conflict,"instruction follows a scheduling terminator"});
    last[block]=&i;
    Set targets;
    for(auto target:i.block_targets)if((!blocks.empty()&&!blocks.contains(target))||!targets.insert(target).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate scheduling branch target"});
    if(!blocks.empty()&&terminal&&targets!=Set(blocks.at(block)->successors.begin(),blocks.at(block)->successors.end()))return Result<int>::err({Error::Code::Conflict,"scheduling terminator targets disagree with CFG successors"});
    if(!terminal&&!targets.empty())return Result<int>::err({Error::Code::InvalidArgument,"block targets require a scheduling terminator"});
    if((i.control==ControlFlow::Branch&&targets.size()!=1)||(i.control==ControlFlow::ConditionalBranch&&targets.size()!=2)||((i.control==ControlFlow::Return||i.control==ControlFlow::Trap)&&!targets.empty()))return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling control-flow target count"});
  }
  for(auto& b:input.blocks)if(b.successors.size()>1&&(!last.contains(b.id)||last.at(b.id)->control!=ControlFlow::ConditionalBranch))return Result<int>::err({Error::Code::Conflict,"multiple scheduling successors require a conditional branch"});
  for(auto& d:input.deps)if(d.kind<DepKind::True||d.kind>DepKind::Ordering||!ids.contains(d.producer)||!ids.contains(d.consumer))return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling dependency"});
  for(auto& d:input.deps)if(d.latency_range&&d.latency_range->minimum>d.latency_range->maximum)return Result<int>::err({Error::Code::InvalidArgument,"reversed dependency latency range"});
  Set group_ids;std::map<InstrId,uint32_t> instruction_blocks;
  for(auto& i:input.instructions)instruction_blocks[i.id]=blocks.empty()?0:i.block;
  for(auto& group:input.groups) {
    if(!group_ids.insert(group.id).second||group.kind<GroupKind::Ordered||group.kind>GroupKind::Pair||group.members.empty()||(group.kind==GroupKind::Pair&&group.members.size()!=2)||(group.kind==GroupKind::Fusion&&group.members.size()<2))return Result<int>::err({Error::Code::InvalidArgument,"invalid scheduling group"});
    if((group.issue_width||!group.issue_slots.empty())&&group.kind!=GroupKind::SameCycle&&group.kind!=GroupKind::Bundle)return Result<int>::err({Error::Code::Unsupported,"group issue constraints require a same-cycle group"});
    Set slots(group.issue_slots.begin(),group.issue_slots.end());if(slots.size()!=group.issue_slots.size())return Result<int>::err({Error::Code::InvalidArgument,"duplicate group issue slot"});
    Set seen;std::optional<uint32_t> block;
    for(auto id:group.members) {
      if(!ids.contains(id)||!seen.insert(id).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate scheduling group member"});
      if(block&&*block!=instruction_blocks.at(id))return Result<int>::err({Error::Code::Unsupported,"scheduling group crosses a block boundary"});
      block=instruction_blocks.at(id);
    }
    uint64_t width=0;for(auto id:group.members)width+=std::find_if(input.instructions.begin(),input.instructions.end(),[&](auto& i){return i.id==id;})->issue_width;
    if(group.issue_width&&width>group.issue_width)return Result<int>::err({Error::Code::Unsatisfiable,"group issue width is smaller than its slot demand"});
  }
  return Result<int>::ok(0);
}
Result<Region> block_region(const Region& input,uint32_t id) {
  std::map<uint32_t,const BasicBlock*> blocks;std::map<InstrId,uint32_t> instructions;
  for(auto& b:input.blocks)if(!blocks.emplace(b.id,&b).second)return Result<Region>::err({Error::Code::InvalidArgument,"duplicate scheduling block"});
  if(!blocks.contains(input.entry)||!blocks.contains(id))return Result<Region>::err({Error::Code::InvalidArgument,"unknown scheduling entry or block"});
  for(auto& b:input.blocks){std::set<uint32_t> seen;for(auto target:b.successors)if(!blocks.contains(target)||!seen.insert(target).second)return Result<Region>::err({Error::Code::InvalidArgument,"unknown or duplicate scheduling successor"});}
  Region local{blocks.at(id)->name};
  for(auto& i:input.instructions){if(!blocks.contains(i.block)||!instructions.emplace(i.id,i.block).second)return Result<Region>::err({Error::Code::InvalidArgument,"unknown block or duplicate scheduling instruction"});if(i.block==id)local.instructions.push_back(i);}
  for(auto& d:input.deps){if(!instructions.contains(d.producer)||!instructions.contains(d.consumer))return Result<Region>::err({Error::Code::InvalidArgument,"unknown CFG dependency"});if(instructions.at(d.producer)==id&&instructions.at(d.consumer)==id)local.deps.push_back(d);}
  for(auto& group:input.groups){bool inside=false,outside=false;for(auto member:group.members){if(!instructions.contains(member))return Result<Region>::err({Error::Code::InvalidArgument,"unknown CFG group member"});(instructions.at(member)==id?inside:outside)=true;}if(inside&&outside)return Result<Region>::err({Error::Code::Unsupported,"scheduling group crosses a block boundary"});if(inside)local.groups.push_back(group);}
  return Result<Region>::ok(std::move(local));
}
Result<int> verify_order(const Region& input,std::span<const InstrId> order,std::span<const std::pair<uint32_t,uint32_t>> physical_aliases) {
  auto hazards=dependencies(input,physical_aliases);if(!hazards)return Result<int>::err(hazards.error());
  std::map<InstrId,const Instruction*> instructions;std::map<InstrId,size_t> positions;std::map<uint32_t,size_t> blocks;
  for(auto& i:input.instructions)instructions[i.id]=&i;
  for(size_t k=0;k<input.blocks.size();++k)blocks[input.blocks[k].id]=k;
  if(order.size()!=instructions.size())return Result<int>::err({Error::Code::Conflict,"incomplete emission order"});
  auto layout=input;layout.instructions.clear();std::optional<size_t> prior_block;
  for(size_t k=0;k<order.size();++k){auto id=order[k];if(!instructions.contains(id)||!positions.emplace(id,k).second)return Result<int>::err({Error::Code::Conflict,"invalid emission order"});auto& i=*instructions.at(id);if(!blocks.empty()){auto block=blocks.at(i.block);if(prior_block&&block<*prior_block)return Result<int>::err({Error::Code::Conflict,"emission order crosses CFG block layout"});prior_block=block;}layout.instructions.push_back(i);}
  auto valid=validate_region(layout);if(!valid)return valid;
  for(auto& d:hazards.value().deps)if(!d.distance&&(input.blocks.empty()||instructions.at(d.producer)->block==instructions.at(d.consumer)->block)&&positions.at(d.producer)>=positions.at(d.consumer))return Result<int>::err({Error::Code::Conflict,"emission order violates a dependency"});
  return verify_group_order(input,order);
}
Result<std::vector<Scheduled>> schedule_cfg(const Region& input,const MachineModel& machine) {
  if(input.blocks.empty())return schedule(input,machine);
  auto valid=validate_region(input);if(!valid)return Result<std::vector<Scheduled>>::err(valid.error());
  std::vector<Scheduled> output;
  for(auto& block:input.blocks){auto region=block_region(input,block.id);if(!region)return Result<std::vector<Scheduled>>::err(region.error());auto result=schedule(region.value(),machine);if(!result)return result;output.insert(output.end(),result.value().begin(),result.value().end());}
  return Result<std::vector<Scheduled>>::ok(std::move(output));
}
Result<int> verify_cfg(const Region& input,const MachineModel& machine,std::span<const Scheduled> schedule) {
  if(input.blocks.empty())return verify(input,machine,schedule);
  auto valid=validate_region(input);if(!valid)return valid;
  std::set<InstrId> seen,expected;std::vector<InstrId> order;for(auto& i:input.instructions)expected.insert(i.id);
  for(auto& s:schedule){if(!expected.contains(s.id)||!seen.insert(s.id).second)return Result<int>::err({Error::Code::Conflict,"invalid CFG schedule identity"});order.push_back(s.id);}
  if(seen!=expected)return Result<int>::err({Error::Code::Conflict,"incomplete CFG schedule"});
  valid=verify_order(input,order,machine.register_aliases);if(!valid)return valid;
  for(auto& block:input.blocks){auto local=block_region(input,block.id);if(!local)return Result<int>::err(local.error());std::vector<Scheduled> entries;for(auto& s:schedule)if(std::any_of(local.value().instructions.begin(),local.value().instructions.end(),[&](auto& i){return i.id==s.id;}))entries.push_back(s);auto result=verify(local.value(),machine,entries);if(!result)return result;}
  return Result<int>::ok(0);
}
}
