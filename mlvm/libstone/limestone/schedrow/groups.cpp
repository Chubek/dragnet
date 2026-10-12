#include "groups.hpp"
#include "slots.hpp"
#include <map>

namespace limestone::schedrow {
Result<int> verify_group_order(const Region& region,std::span<const InstrId> order) {
  auto valid=validate_region(region);if(!valid)return valid;
  std::map<InstrId,size_t> positions;
  for(size_t k=0;k<order.size();++k)if(!positions.emplace(order[k],k).second)return Result<int>::err({Error::Code::Conflict,"duplicate group emission identity"});
  for(auto& group:region.groups) {
    std::optional<size_t> prior;
    for(auto id:group.members) {
      if(!positions.contains(id))return Result<int>::err({Error::Code::Conflict,"missing group member: "+std::to_string(group.id)});
      auto position=positions.at(id);
      if(prior&&(position<=*prior||(detail::adjacent(group.kind)&&position!=*prior+1)))return Result<int>::err({Error::Code::Conflict,"group order/adjacency violation: "+std::to_string(group.id)});
      prior=position;
    }
  }
  return Result<int>::ok(0);
}
Result<int> verify_groups(const Region& region,std::span<const Scheduled> entries) {
  auto valid=validate_region(region);if(!valid)return valid;
  std::map<InstrId,uint32_t> cycles,blocks;std::map<uint32_t,size_t> layout;
  for(auto& i:region.instructions)blocks[i.id]=region.blocks.empty()?0:i.block;
  if(region.blocks.empty())layout[0]=0;else for(size_t k=0;k<region.blocks.size();++k)layout[region.blocks[k].id]=k;
  for(auto& s:entries)if(!blocks.contains(s.id)||!cycles.emplace(s.id,s.cycle).second)return Result<int>::err({Error::Code::Conflict,"invalid group schedule identity"});
  for(auto& group:region.groups) {
    std::optional<uint32_t> prior;
    for(auto id:group.members) {
      if(!cycles.contains(id))return Result<int>::err({Error::Code::Conflict,"missing scheduled group member"});
      auto cycle=cycles.at(id);
      if(prior&&(cycle<*prior||(detail::same_cycle(group.kind)&&cycle!=*prior)))return Result<int>::err({Error::Code::Conflict,"group cycle violation: "+std::to_string(group.id)});
      prior=cycle;
      if(!group.issue_slots.empty()){auto scheduled=std::find_if(entries.begin(),entries.end(),[&](auto& s){return s.id==id;});if(!scheduled->slot)return Result<int>::err({Error::Code::Conflict,"group issue-slot violation"});for(auto slot:detail::assigned_slots(*scheduled))if(std::find(group.issue_slots.begin(),group.issue_slots.end(),slot)==group.issue_slots.end())return Result<int>::err({Error::Code::Conflict,"group issue-slot violation"});}
    }
  }
  std::vector<Scheduled> sorted(entries.begin(),entries.end());
  std::stable_sort(sorted.begin(),sorted.end(),[&](auto& a,auto& b){return std::pair{layout.at(blocks.at(a.id)),a.cycle}<std::pair{layout.at(blocks.at(b.id)),b.cycle};});
  std::vector<InstrId> order;for(auto& s:sorted)order.push_back(s.id);
  return verify_group_order(region,order);
}
}
