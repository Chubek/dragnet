#include "regtl.hpp"
#include <limits>
#include <map>
#include <set>

namespace limestone::regtl {
Result<Liveness> analyze(const Function& function) {
  Program register_model;register_model.classes=function.classes;register_model.aliases=function.aliases;register_model.storage=function.storage;
  using Set=std::set<VReg>;
  std::map<VReg,const VirtualRegister*> values;
  std::map<uint32_t,const Block*> blocks;
  std::set<uint32_t> instructions;
  std::map<uint32_t,Set> use,def,in,out;
  std::map<uint32_t,Set> physical_use,physical_def,physical_in,physical_out;
  std::set<PReg> physical;
  for(auto& klass:function.classes)physical.insert(klass.members.begin(),klass.members.end());
  for(auto& value:function.values)if(!values.emplace(value.value,&value).second)return Result<Liveness>::err({Error::Code::InvalidArgument,"duplicate virtual register"});
  for(auto& block:function.blocks)if(!blocks.emplace(block.id,&block).second)return Result<Liveness>::err({Error::Code::InvalidArgument,"duplicate allocation block"});
  if(blocks.empty())return Result<Liveness>::err({Error::Code::InvalidArgument,"function has no blocks"});
  for(auto& block:function.blocks) {
    std::set<uint32_t> successors;
    for(auto successor:block.successors)if(!blocks.contains(successor)||!successors.insert(successor).second)return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown or duplicate allocation successor"});
    for(auto value:block.live_out)if(!values.contains(value))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown live-out value"});
    for(auto reg:block.physical_live_out)if(!physical.contains(reg))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown physical live-out register"});
    for(auto& instruction:block.instructions) {
      if(!instructions.insert(instruction.id).second)return Result<Liveness>::err({Error::Code::InvalidArgument,"duplicate allocation instruction"});
      std::function<bool(const TransferOperand&,bool)> transfer_operand=[&](const TransferOperand& operand,bool written){
        switch(operand.kind){case TransferOperand::Kind::Virtual:return values.contains(operand.id)&&std::find((written?instruction.defs:instruction.uses).begin(),(written?instruction.defs:instruction.uses).end(),operand.id)!=(written?instruction.defs:instruction.uses).end();case TransferOperand::Kind::Physical:return physical.contains(operand.id)&&std::find((written?instruction.physical_defs:instruction.physical_uses).begin(),(written?instruction.physical_defs:instruction.physical_uses).end(),operand.id)!=(written?instruction.physical_defs:instruction.physical_uses).end();case TransferOperand::Kind::Immediate:return !written&&operand.address.empty();case TransferOperand::Kind::Spill:return operand.address.empty();case TransferOperand::Kind::Memory:return operand.address.size()==1&&transfer_operand(operand.address[0],false);}return false;
      };
      for(size_t k=0;k<instruction.transfers.size();++k){auto& transfer=instruction.transfers[k];if(!transfer_operand(transfer.destination,true)||!transfer_operand(transfer.source,false))return Result<Liveness>::err({Error::Code::Conflict,"transfer operands disagree with allocation dataflow"});if(instruction.parallel)for(size_t j=0;j<k;++j){auto& a=instruction.transfers[j].destination;auto& b=transfer.destination;bool overlap=a==b;if(a.kind==TransferOperand::Kind::Physical&&b.kind==TransferOperand::Kind::Physical)overlap|=registers_overlap(register_model,a.id,b.id);if(overlap)return Result<Liveness>::err({Error::Code::Conflict,"overlapping parallel transfer destinations"});}}
      for(auto reg:instruction.clobbers)if(!physical.contains(reg))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown clobbered physical register"});
      for(auto reg:instruction.physical_uses){if(!physical.contains(reg))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown implicit physical input"});if(!physical_def[block.id].contains(reg))physical_use[block.id].insert(reg);}
      for(auto reg:instruction.physical_defs){if(!physical.contains(reg))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown implicit physical definition"});physical_def[block.id].insert(reg);}
      physical_def[block.id].insert(instruction.clobbers.begin(),instruction.clobbers.end());
      for(auto value:instruction.uses) {
        if(!values.contains(value))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown used virtual register"});
        if(!def[block.id].contains(value))use[block.id].insert(value);
      }
      for(auto value:instruction.defs) {
        if(!values.contains(value))return Result<Liveness>::err({Error::Code::InvalidArgument,"unknown defined virtual register"});
        def[block.id].insert(value);
      }
      for(auto value:instruction.early_defs)if(std::find(instruction.defs.begin(),instruction.defs.end(),value)==instruction.defs.end())return Result<Liveness>::err({Error::Code::InvalidArgument,"early definition is not a definition"});
      for(auto [a,b]:instruction.ties)if(std::find(instruction.defs.begin(),instruction.defs.end(),a)==instruction.defs.end()||std::find(instruction.uses.begin(),instruction.uses.end(),b)==instruction.uses.end())return Result<Liveness>::err({Error::Code::InvalidArgument,"tie must connect a definition to an input"});
    }
  }
  bool changed=true;
  while(changed) {
    changed=false;
    for(auto it=blocks.rbegin();it!=blocks.rend();++it) {
      const auto& block=*it->second;Set next_out(block.live_out.begin(),block.live_out.end());
      for(auto successor:block.successors)next_out.insert(in[successor].begin(),in[successor].end());
      Set next_in=use[block.id];for(auto value:next_out)if(!def[block.id].contains(value))next_in.insert(value);
       if(next_in!=in[block.id]||next_out!=out[block.id]){changed=true;in[block.id]=std::move(next_in);out[block.id]=std::move(next_out);}
      Set next_physical_out(block.physical_live_out.begin(),block.physical_live_out.end());for(auto successor:block.successors)next_physical_out.insert(physical_in[successor].begin(),physical_in[successor].end());
      Set next_physical_in=physical_use[block.id];for(auto reg:next_physical_out)if(!physical_def[block.id].contains(reg))next_physical_in.insert(reg);
      if(next_physical_in!=physical_in[block.id]||next_physical_out!=physical_out[block.id]){changed=true;physical_in[block.id]=std::move(next_physical_in);physical_out[block.id]=std::move(next_physical_out);}
    }
  }
  Liveness result;auto& problem=result.problem;problem.classes=function.classes;problem.aliases=function.aliases;problem.reserved=function.reserved;problem.explicit_interference=true;
  problem.storage=function.storage;problem.tuples=function.tuples;
  std::set<std::pair<VReg,VReg>> edges,ties;
  std::map<VReg,Set> forbidden;
  std::map<VReg,std::set<uint32_t>> positions;
  uint64_t base=0;
  auto clique=[&](const Set& live){for(auto a=live.begin();a!=live.end();++a)for(auto b=std::next(a);b!=live.end();++b)edges.emplace(*a,*b);};
  auto exclude=[&](const Set& live,const Set& state){for(auto value:live)forbidden[value].insert(state.begin(),state.end());};
  for(auto [id,ptr]:blocks) {
    const auto& block=*ptr;
    if(base+2*block.instructions.size()+2>std::numeric_limits<uint32_t>::max())return Result<Liveness>::err({Error::Code::ResourceLimit,"allocation position overflow"});
    result.live_in[id]={in[id].begin(),in[id].end()};result.live_out[id]={out[id].begin(),out[id].end()};
    result.physical_live_in[id]={physical_in[id].begin(),physical_in[id].end()};result.physical_live_out[id]={physical_out[id].begin(),physical_out[id].end()};
    Set live=out[id];clique(live);
    Set state=physical_out[id];exclude(live,state);
    for(auto value:live)positions[value].insert(static_cast<uint32_t>(base+2*block.instructions.size()));
    for(size_t k=block.instructions.size();k-->0;) {
      const auto& instruction=block.instructions[k];auto point=static_cast<uint32_t>(base+2*k);
      result.physical_after[instruction.id]={state.begin(),state.end()};
      Set after=live;after.insert(instruction.defs.begin(),instruction.defs.end());clique(after);
      Set written=state;written.insert(instruction.physical_defs.begin(),instruction.physical_defs.end());exclude(after,written);
      for(auto value:after)positions[value].insert(point+1);
      for(auto value:live)if(std::find(instruction.defs.begin(),instruction.defs.end(),value)==instruction.defs.end())forbidden[value].insert(instruction.clobbers.begin(),instruction.clobbers.end());
      for(auto value:instruction.defs)live.erase(value);
      live.insert(instruction.uses.begin(),instruction.uses.end());
      Set early=live;early.insert(instruction.early_defs.begin(),instruction.early_defs.end());clique(early);
      for(auto reg:instruction.physical_defs)state.erase(reg);for(auto reg:instruction.clobbers)state.erase(reg);state.insert(instruction.physical_uses.begin(),instruction.physical_uses.end());exclude(early,state);
      result.physical_before[instruction.id]={state.begin(),state.end()};
      for(auto value:early)positions[value].insert(point);
      for(auto [a,b]:instruction.ties)if(a!=b)ties.emplace(std::min(a,b),std::max(a,b));
    }
    base+=2*block.instructions.size()+2;
  }
  for(auto [id,value]:values) {
    auto constraint=value->constraint;
    for(auto physical:forbidden[id]) {
      constraint.forbidden.push_back(physical);
      for(auto& klass:function.classes)for(auto reg:klass.members)if(reg!=physical&&registers_overlap(register_model,reg,physical))constraint.forbidden.push_back(reg);
    }
    auto& points=positions[id];if(points.empty())points.insert(0);
    std::vector<std::pair<uint32_t,uint32_t>> segments;
    for(auto point:points) {
      if(segments.empty()||uint64_t(segments.back().second)+1<point)segments.emplace_back(point,point);
      else segments.back().second=point;
    }
    result.segments[id]=segments;
    problem.ranges.push_back({id,*points.begin(),*points.rbegin(),value->klass,std::move(constraint),value->spillable});
  }
  problem.interference={edges.begin(),edges.end()};problem.ties={ties.begin(),ties.end()};
  auto checked=validate(problem);if(!checked)return Result<Liveness>::err(checked.error());
  return Result<Liveness>::ok(std::move(result));
}

Result<std::vector<Move>> resolve_parallel_moves(std::span<const Move> input,Location scratch) {
  std::map<Location,Location> pending;
  for(auto move:input) {
    if(move.destination==scratch||move.source==scratch)return Result<std::vector<Move>>::err({Error::Code::InvalidArgument,"scratch overlaps parallel moves"});
    if(!pending.emplace(move.destination,move.source).second)return Result<std::vector<Move>>::err({Error::Code::Conflict,"duplicate parallel-move destination"});
  }
  std::erase_if(pending,[](auto move){return move.first==move.second;});
  std::vector<Move> result;
  while(!pending.empty()) {
    auto ready=std::find_if(pending.begin(),pending.end(),[&](auto move){return std::none_of(pending.begin(),pending.end(),[&](auto other){return other.second==move.first;});});
    if(ready!=pending.end()){result.push_back({ready->first,ready->second});pending.erase(ready);continue;}
    auto saved=pending.begin()->first;result.push_back({scratch,saved});
    for(auto& [destination,source]:pending)if(source==saved)source=scratch;
  }
  return Result<std::vector<Move>>::ok(std::move(result));
}
}
