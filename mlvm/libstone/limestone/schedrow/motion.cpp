#include "motion.hpp"
#include <map>
#include <set>

namespace limestone::schedrow {
namespace {
using Set=std::set<uint32_t>;
struct Flow { std::map<uint32_t,Set> dominators;Set reachable; };
struct Budget {
  size_t remaining;
  void charge(size_t amount=1){if(amount>remaining)throw Error{Error::Code::ResourceLimit,"cross-block motion work limit"};remaining-=amount;}
};
Flow flow(const Region& region,Budget& budget) {
  if(region.blocks.empty())throw Error{Error::Code::InvalidArgument,"cross-block motion requires an explicit CFG"};
  std::map<uint32_t,const BasicBlock*> blocks;std::map<uint32_t,Set> predecessors;
  for(auto& block:region.blocks){budget.charge();blocks[block.id]=&block;for(auto successor:block.successors){budget.charge();predecessors[successor].insert(block.id);}}
  Flow result;std::vector<uint32_t> work{region.entry};
  while(!work.empty()){budget.charge();auto id=work.back();work.pop_back();if(result.reachable.insert(id).second)for(auto successor:blocks.at(id)->successors)work.push_back(successor);}
  for(auto& block:region.blocks){budget.charge(result.reachable.size());result.dominators[block.id]=block.id==region.entry||!result.reachable.contains(block.id)?Set{block.id}:result.reachable;}
  bool changed=true;
  while(changed){changed=false;for(auto id:result.reachable)if(id!=region.entry){budget.charge(result.reachable.size());auto next=result.reachable;
      for(auto predecessor:predecessors[id])if(result.reachable.contains(predecessor)){Set intersection;for(auto item:next){budget.charge();if(result.dominators.at(predecessor).contains(item))intersection.insert(item);}next=std::move(intersection);}
      next.insert(id);if(next!=result.dominators.at(id)){result.dominators[id]=std::move(next);changed=true;}
    }}
  return result;
}
void check_ssa(const Region& region,const Flow& cfg,Budget& budget) {
  std::map<ValueId,const Instruction*> definitions;std::map<InstrId,size_t> positions;std::map<InstrId,const Instruction*> instructions;
  for(size_t k=0;k<region.instructions.size();++k){budget.charge();auto& instruction=region.instructions[k];positions[instruction.id]=k;instructions[instruction.id]=&instruction;for(auto value:instruction.defs){budget.charge();if(!definitions.emplace(value,&instruction).second)throw Error{Error::Code::Conflict,"motion requires unique SSA definitions"};}}
  auto before=[&](const Instruction& producer,const Instruction& consumer){return producer.block==consumer.block?positions.at(producer.id)<positions.at(consumer.id):cfg.dominators.at(consumer.block).contains(producer.block);};
  for(auto& instruction:region.instructions)for(auto value:instruction.uses){budget.charge();if(definitions.contains(value)&&!before(*definitions.at(value),instruction))throw Error{Error::Code::Conflict,"motion value does not dominate its use: "+std::to_string(value)};}
  for(auto& block:region.blocks)for(auto value:block.live_out){budget.charge();if(definitions.contains(value)&&!cfg.dominators.at(block.id).contains(definitions.at(value)->block))throw Error{Error::Code::Conflict,"motion value does not dominate a live-out"};}
  for(auto& edge:region.deps){budget.charge();if(edge.distance)throw Error{Error::Code::Unsupported,"motion of loop-carried dependence needs an iteration adapter"};if(!edge.scheduler_only&&!before(*instructions.at(edge.producer),*instructions.at(edge.consumer)))throw Error{Error::Code::Conflict,"motion violates a semantic dependence"};}
}
}
Result<int> verify_ssa(const Region& input,size_t work_limit) {
  auto valid=validate_region(input);if(!valid)return valid;
  try {Budget budget{work_limit};auto cfg=flow(input,budget);check_ssa(input,cfg,budget);return Result<int>::ok(0);}
  catch(const Error& error){return Result<int>::err(error);}
}
Result<Region> move_instruction(const Region& input,const MotionRequest& request,const MotionOptions& options) {
  using Output=Result<Region>;auto valid=validate_region(input);if(!valid)return Output::err(valid.error());
  try {
    Budget budget{options.work_limit};auto cfg=flow(input,budget);check_ssa(input,cfg,budget);
    auto found=std::find_if(input.instructions.begin(),input.instructions.end(),[&](auto& instruction){return instruction.id==request.instruction;});
    if(found==input.instructions.end()||!cfg.dominators.contains(request.block))throw Error{Error::Code::NotFound,"unknown motion instruction or destination block"};
    if(!cfg.reachable.contains(found->block)||!cfg.reachable.contains(request.block))throw Error{Error::Code::Conflict,"motion across unreachable blocks"};
    if(found->terminator||found->control!=ControlFlow::None||found->call||found->barrier)throw Error{Error::Code::Unsupported,"control boundaries cannot move as ordinary instructions"};
    for(auto& group:input.groups)if(std::find(group.members.begin(),group.members.end(),found->id)!=group.members.end())throw Error{Error::Code::Conflict,"motion must preserve a grouped instruction as a unit"};
    bool pure=found->speculative&&!found->may_trap&&!found->memory&&!found->access&&found->implicit_defs.empty()&&found->implicit_uses.empty();
    if(!pure){if(!options.prove)throw Error{Error::Code::Unsupported,"motion requires a target semantic proof"};auto proved=options.prove(input,request);if(!proved)return Output::err(proved.error());if(!proved.value())throw Error{Error::Code::Conflict,"target rejected motion safety"};}
    auto augmented=dependencies(input);if(!augmented)return Output::err(augmented.error());
    auto result=input;result.instructions.clear();
    // Generated true/storage/memory edges retain their meaning. Generated
    // terminator placement and scheduler-only barriers are rebuilt at destination.
    for(auto& edge:augmented.value().deps)if(!edge.scheduler_only&&edge.kind!=DepKind::Control&&std::none_of(result.deps.begin(),result.deps.end(),[&](auto& existing){return existing.producer==edge.producer&&existing.consumer==edge.consumer&&existing.kind==edge.kind;}))result.deps.push_back(edge);
    std::erase_if(result.deps,[&](auto& edge){return edge.scheduler_only&&(edge.producer==request.instruction||edge.consumer==request.instruction);});
    bool inserted=false;
    for(auto& block:input.blocks) {
      for(auto& instruction:input.instructions)if(instruction.block==block.id&&instruction.id!=request.instruction) {
        bool at=request.before?instruction.id==*request.before:instruction.terminator;
        if(block.id==request.block&&!inserted&&at){auto moved=*found;moved.block=request.block;result.instructions.push_back(std::move(moved));inserted=true;}
        result.instructions.push_back(instruction);
      }
      if(block.id==request.block&&!inserted&&!request.before){auto moved=*found;moved.block=request.block;result.instructions.push_back(std::move(moved));inserted=true;}
    }
    if(!inserted)throw Error{Error::Code::InvalidArgument,"motion insertion point is not in the destination block"};
    valid=validate_region(result);if(!valid)return Output::err(valid.error());check_ssa(result,cfg,budget);
    std::vector<InstrId> order;for(auto& instruction:result.instructions)order.push_back(instruction.id);valid=verify_group_order(result,order);if(!valid)return Output::err(valid.error());
    return Output::ok(std::move(result));
  }catch(const Error& error){return Output::err(error);}
  catch(const std::exception& error){return Output::err({Error::Code::Internal,error.what()});}
  catch(...){return Output::err({Error::Code::Internal,"motion proof exception"});}
}
}
