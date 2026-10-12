#pragma once
#include "schedrow.hpp"
namespace limestone::schedrow::detail {
using SlotRequest=std::pair<const Instruction*,std::vector<uint32_t>>;
Result<bool> slots_feasible(uint32_t width,const std::vector<SlotRequest>&,size_t search_limit=1000000);
inline std::vector<uint32_t> assigned_slots(const Scheduled& entry) {
  auto result=entry.additional_slots;if(entry.slot)result.insert(result.begin(),*entry.slot);return result;
}
// Enumerate distinct slot sets in stable order. The caller owns reservations and
// can backtrack jointly over slots and resources. No contiguous-slot assumption.
template<class Set,class Accept>
bool choose_slots(const Instruction& instruction,uint32_t width,const Set& occupied,
                  Accept accept,const std::function<bool()>& charge={}) {
  std::vector<uint32_t> selected;
  if(!width)return instruction.issue_width==1&&accept(selected);
  auto allowed=instruction.issue_slots;std::sort(allowed.begin(),allowed.end());
  const uint64_t end=allowed.empty()?width:allowed.size();
  if(instruction.issue_width>end)return false;
  if(allowed.empty()&&(occupied.size()>width||instruction.issue_width>width-occupied.size()))return false;
  // Iterative backtracking avoids recursion proportional to the machine width.
  std::vector<uint64_t> indices;uint64_t index=0;
  for(;;) {
    if(charge&&!charge())return false;
    if(selected.size()==instruction.issue_width){if(accept(selected))return true;index=end;}
    bool advanced=false;
    for(;index<end;++index){if(charge&&!charge())return false;auto slot=allowed.empty()?uint32_t(index):allowed[index];if(occupied.contains(slot))continue;
      if(end-index<instruction.issue_width-selected.size())break;
      selected.push_back(slot);indices.push_back(index++);advanced=true;break;
    }
    if(advanced)continue;
    if(indices.empty())return false;
    index=indices.back()+1;indices.pop_back();selected.pop_back();
  }
}
inline void set_slots(Scheduled& entry,const std::vector<uint32_t>& slots) {
  entry.slot.reset();entry.additional_slots.clear();
  if(!slots.empty()){entry.slot=slots.front();entry.additional_slots.assign(slots.begin()+1,slots.end());}
}
}
