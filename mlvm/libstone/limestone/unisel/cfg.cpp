#include "unisel.hpp"
#include <map>
#include <set>

namespace limestone::unisel {
Result<int> validate_cfg(const Program& p) {
  using Set=std::set<uint32_t>;std::map<uint32_t,const schedrow::BasicBlock*> blocks;
  std::map<NodeId,const Node*> nodes;for(auto& n:p.nodes)nodes[n.id]=&n;
  for(auto& n:p.nodes){if(n.control<schedrow::ControlFlow::None||n.control>schedrow::ControlFlow::Call)return Result<int>::err({Error::Code::InvalidArgument,"invalid source control flow"});if(n.access&&(n.access->ordering<schedrow::MemoryOrdering::Relaxed||n.access->ordering>schedrow::MemoryOrdering::Sequential||(n.access->alignment&&(n.access->alignment&(n.access->alignment-1)))))return Result<int>::err({Error::Code::InvalidArgument,"invalid source memory contract"});}
  if(p.blocks.empty()) {
    Set present;for(auto& n:p.nodes)if(n.required)present.insert(n.block);
    if(present.size()>1)return Result<int>::err({Error::Code::InvalidArgument,"multi-block source needs explicit CFG blocks"});
    for(auto& n:p.nodes)if(!n.block_targets.empty())return Result<int>::err({Error::Code::InvalidArgument,"branch targets need explicit CFG blocks"});
    return Result<int>::ok(0);
  }
  std::map<uint32_t,Set> predecessors,dominators;Set all,reachable;
  for(auto& b:p.blocks)if(!blocks.emplace(b.id,&b).second)return Result<int>::err({Error::Code::InvalidArgument,"duplicate source block"});else all.insert(b.id);
  if(!blocks.contains(p.entry))return Result<int>::err({Error::Code::InvalidArgument,"unknown source entry block"});
  if(p.blocks.front().id!=p.entry)return Result<int>::err({Error::Code::Conflict,"entry block must lead source layout"});
  for(auto& b:p.blocks){Set seen;for(auto target:b.successors){if(!blocks.contains(target)||!seen.insert(target).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate source successor"});predecessors[target].insert(b.id);}for(auto value:b.live_out)if(!nodes.contains(value)||!nodes.at(value)->produces_value)return Result<int>::err({Error::Code::InvalidArgument,"unknown source live-out value"});}
  std::vector<uint32_t> pending{p.entry};while(!pending.empty()){auto id=pending.back();pending.pop_back();if(reachable.insert(id).second)for(auto next:blocks.at(id)->successors)pending.push_back(next);}
  for(auto id:all)dominators[id]=id==p.entry||!reachable.contains(id)?Set{id}:reachable;
  bool changed=true;while(changed){changed=false;for(auto id:reachable)if(id!=p.entry){Set next=reachable;for(auto predecessor:predecessors[id])if(reachable.contains(predecessor)){Set intersection;std::set_intersection(next.begin(),next.end(),dominators[predecessor].begin(),dominators[predecessor].end(),std::inserter(intersection,intersection.end()));next=std::move(intersection);}next.insert(id);if(next!=dominators[id]){dominators[id]=std::move(next);changed=true;}}}
  auto dominates=[&](NodeId value,uint32_t block){auto& n=*nodes.at(value);return !n.required||dominators[block].contains(n.block);};
  std::map<uint32_t,const Node*> last;
  for(auto& n:p.nodes) {
    if(!blocks.contains(n.block))return Result<int>::err({Error::Code::InvalidArgument,"node belongs to unknown source block"});
    if(n.control<schedrow::ControlFlow::None||n.control>schedrow::ControlFlow::Call)return Result<int>::err({Error::Code::InvalidArgument,"invalid source control flow"});
    bool terminal=n.terminator||(n.control!=schedrow::ControlFlow::None&&n.control!=schedrow::ControlFlow::Call);
    if(n.required){if(last.contains(n.block)&&(last[n.block]->terminator||(last[n.block]->control!=schedrow::ControlFlow::None&&last[n.block]->control!=schedrow::ControlFlow::Call)))return Result<int>::err({Error::Code::Conflict,"operation follows a source terminator"});last[n.block]=&n;}
    for(auto value:n.inputs)if(nodes.contains(value)&&!dominates(value,n.block))return Result<int>::err({Error::Code::Conflict,"v"+std::to_string(value)+" does not dominate its use"});
    Set targets;for(auto target:n.block_targets)if(!blocks.contains(target)||!targets.insert(target).second)return Result<int>::err({Error::Code::InvalidArgument,"invalid source branch target"});
    if(terminal){Set successors(blocks.at(n.block)->successors.begin(),blocks.at(n.block)->successors.end());if(targets!=successors)return Result<int>::err({Error::Code::Conflict,"terminator targets disagree with CFG successors"});}
    else if(!targets.empty())return Result<int>::err({Error::Code::InvalidArgument,"block targets require a terminator"});
    if((n.control==schedrow::ControlFlow::Branch&&targets.size()!=1)||(n.control==schedrow::ControlFlow::ConditionalBranch&&targets.size()!=2)||((n.control==schedrow::ControlFlow::Return||n.control==schedrow::ControlFlow::Trap)&&!targets.empty()))return Result<int>::err({Error::Code::InvalidArgument,"invalid control-flow target count"});
  }
  for(auto& b:p.blocks){if(b.successors.size()>1&&(!last.contains(b.id)||(last[b.id]->control!=schedrow::ControlFlow::ConditionalBranch)))return Result<int>::err({Error::Code::Conflict,"multiple CFG successors require a conditional branch"});for(auto value:b.live_out)if(!dominates(value,b.id))return Result<int>::err({Error::Code::Conflict,"value does not dominate a live-out boundary"});if(b.successors.empty())for(auto value:p.outputs)if(nodes.contains(value)&&!dominates(value,b.id))return Result<int>::err({Error::Code::Conflict,"output value does not dominate every exit"});}
  return Result<int>::ok(0);
}
Result<Program> prepare(const Program& input) {
  auto checked=validate(input,{});if(!checked)return Result<Program>::err(checked.error());
  schedrow::Region effects{"source-effects"};effects.blocks=input.blocks;effects.entry=input.entry;
  for(auto& n:input.nodes)if(n.required&&(n.side_effect||n.access||n.call||n.terminator||n.may_trap||n.control!=schedrow::ControlFlow::None)){
    schedrow::Instruction i{};i.id=n.id;i.opcode=n.op;i.block=n.block;i.control=n.control;i.block_targets=n.block_targets;i.access=n.access;i.memory=bool(n.access);i.may_trap=n.may_trap;i.speculative=false;i.call=n.call||n.control==schedrow::ControlFlow::Call;i.terminator=n.terminator||(n.control!=schedrow::ControlFlow::None&&n.control!=schedrow::ControlFlow::Call);i.barrier=n.side_effect&&!n.access;effects.instructions.push_back(std::move(i));
  }
  auto ordered=schedrow::dependencies(effects);if(!ordered)return Result<Program>::err(ordered.error());auto result=input;
  for(auto d:ordered.value().deps){d.scheduler_only=false;if(std::none_of(result.dependencies.begin(),result.dependencies.end(),[&](auto& e){return e.producer==d.producer&&e.consumer==d.consumer&&e.kind==d.kind&&!e.distance&&!e.scheduler_only&&e.latency>=d.latency;}))result.dependencies.push_back(d);}
  checked=validate(result,{});if(!checked)return Result<Program>::err(checked.error());return Result<Program>::ok(std::move(result));
}
}
