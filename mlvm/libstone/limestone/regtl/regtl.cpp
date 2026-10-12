#include "regtl.hpp"
#include <map>

namespace limestone::regtl {
bool registers_overlap(const Program& p,PReg a,PReg b) {
  if(a==b||std::any_of(p.aliases.begin(),p.aliases.end(),[&](auto pair){return pair==std::pair{a,b}||pair==std::pair{b,a};}))return true;
  auto first=std::find_if(p.storage.begin(),p.storage.end(),[&](auto& r){return r.id==a;});
  auto second=std::find_if(p.storage.begin(),p.storage.end(),[&](auto& r){return r.id==b;});
  if(first==p.storage.end()||second==p.storage.end())return false;
  for(auto& x:first->slices)for(auto& y:second->slices)if(x.unit==y.unit&&uint64_t(x.begin)<uint64_t(y.begin)+y.width&&uint64_t(y.begin)<uint64_t(x.begin)+x.width)return true;
  return false;
}
std::vector<PReg> allowed_registers(const Program& p,const LiveRange& r) {
  std::vector<PReg> regs;for(auto& c:p.classes)if(c.name==r.klass)regs=c.members;
  std::erase_if(regs,[&](PReg x){
    if(r.constraint.fixed&&x!=*r.constraint.fixed)return true;
    if(!r.constraint.allowed.empty()&&std::find(r.constraint.allowed.begin(),r.constraint.allowed.end(),x)==r.constraint.allowed.end())return true;
    if(std::find(r.constraint.forbidden.begin(),r.constraint.forbidden.end(),x)!=r.constraint.forbidden.end())return true;
    if(!r.constraint.bank.empty()){auto storage=std::find_if(p.storage.begin(),p.storage.end(),[&](auto& s){return s.id==x;});if(storage==p.storage.end()||storage->bank!=r.constraint.bank)return true;}
    for(auto reserved:p.reserved)if(registers_overlap(p,x,reserved))return true;
    for(auto& c:p.clobbers)if(r.begin<c.position&&c.position<r.end)for(auto clobbered:c.registers)if(registers_overlap(p,x,clobbered))return true;
    return false;
  });std::sort(regs.begin(),regs.end());return regs;
}
std::vector<std::pair<PReg,PReg>> register_aliases(const Program& p) {
  std::vector<PReg> registers;for(auto& c:p.classes)registers.insert(registers.end(),c.members.begin(),c.members.end());
  std::sort(registers.begin(),registers.end());registers.erase(std::unique(registers.begin(),registers.end()),registers.end());
  std::vector<std::pair<PReg,PReg>> result;
  for(size_t i=0;i<registers.size();++i)for(size_t j=i+1;j<registers.size();++j)if(registers_overlap(p,registers[i],registers[j]))result.emplace_back(registers[i],registers[j]);
  return result;
}
Result<Program> with_fixed_registers(const Program& program,std::span<const std::pair<VReg,PReg>> requirements) {
  auto checked=validate(program);if(!checked)return Result<Program>::err(checked.error());auto result=program;
  for(auto [value,physical]:requirements) {
    auto range=std::find_if(result.ranges.begin(),result.ranges.end(),[&](auto& r){return r.value==value;});
    if(range==result.ranges.end())return Result<Program>::err({Error::Code::Conflict,"fixed operand references missing allocation value v"+std::to_string(value)});
    if(range->constraint.fixed&&*range->constraint.fixed!=physical)return Result<Program>::err({Error::Code::Conflict,"conflicting fixed registers for v"+std::to_string(value)});
    range->constraint.fixed=physical;
  }
  checked=validate(result);if(!checked)return Result<Program>::err(checked.error());return Result<Program>::ok(std::move(result));
}
namespace {
bool overlaps(const Program& p,const LiveRange& a,const LiveRange& b) {
  if(p.explicit_interference)return std::any_of(p.interference.begin(),p.interference.end(),[&](auto e){return e==std::pair{a.value,b.value}||e==std::pair{b.value,a.value};});
  return a.begin<=b.end&&b.begin<=a.end;
}
bool aliases(const Program& p,PReg a,PReg b) {
  return registers_overlap(p,a,b);
}
std::vector<PReg> legal(const Program& p,const LiveRange& r) {
  return allowed_registers(p,r);
}
Result<Allocation> assign(const Program& p,std::vector<const LiveRange*> order) {
  auto v=validate(p);if(!v)return Result<Allocation>::err(v.error());
  Allocation a;
  std::map<VReg,const LiveRange*> ranges;
  for(auto& r:p.ranges)ranges[r.value]=&r;
  for(auto r:order) {
    auto available=legal(p,*r);
    std::optional<PReg> chosen;
    for(auto x:available) {
      bool busy=false;
     for(auto& [value,reg]:a.regs)if(overlaps(p,*r,*ranges.at(value))&&aliases(p,x,reg)){busy=true;break;}
      // Reserve fixed locations for intervals that have not been visited yet.
     for(auto& other:p.ranges)if(other.value!=r->value&&other.constraint.fixed&&overlaps(p,*r,other)&&aliases(p,x,*other.constraint.fixed)){busy=true;break;}
      if(!busy){chosen=x;break;}
    }
    if(!chosen&&(!r->spillable||r->constraint.fixed)) {
      for(auto x:available) {
        std::vector<VReg> victims;bool possible=true;
         for(auto& [value,reg]:a.regs)if(overlaps(p,*r,*ranges.at(value))&&aliases(p,x,reg)) {
          auto other=ranges.at(value);
          if(!other->spillable||other->constraint.fixed){possible=false;break;}
          victims.push_back(value);
        }
         for(auto& other:p.ranges)if(other.value!=r->value&&other.constraint.fixed&&overlaps(p,*r,other)&&aliases(p,x,*other.constraint.fixed))possible=false;
        if(possible) {
          for(auto victim:victims){a.regs.erase(victim);a.spilled.push_back(victim);}
          chosen=x;break;
        }
      }
    }
    if(chosen)a.regs[r->value]=*chosen;
    else if(r->spillable&&!r->constraint.fixed)a.spilled.push_back(r->value);
    else return Result<Allocation>::err({Error::Code::Unsatisfiable,"no legal register for v"+std::to_string(r->value)+" in "+r->klass});
  }
  std::sort(a.spilled.begin(),a.spilled.end());
  auto verified=verify(p,a);if(!verified)return Result<Allocation>::err(verified.error());
  return Result<Allocation>::ok(std::move(a));
}
}
Result<int> validate(const Program& p) {
  if(!p.explicit_interference&&!p.interference.empty())return Result<int>::err({Error::Code::InvalidArgument,"explicit interference edges need the explicit-interference policy"});
  std::unordered_set<std::string> classes;std::unordered_set<PReg> physical;
  for(auto& c:p.classes) {
    if(c.name.empty()||c.members.empty()||!classes.insert(c.name).second)return Result<int>::err({Error::Code::InvalidArgument,"invalid or duplicate register class: "+c.name});
    std::unordered_set<PReg> members;
    for(auto r:c.members){physical.insert(r);if(!members.insert(r).second)return Result<int>::err({Error::Code::InvalidArgument,"duplicate physical register in class"});}
  }
  for(auto [a,b]:p.aliases)if(!physical.contains(a)||!physical.contains(b))return Result<int>::err({Error::Code::InvalidArgument,"unknown register alias"});
  std::unordered_set<PReg> described;std::unordered_set<std::string> banks;
  for(auto& r:p.storage){if(!physical.contains(r.id)||!described.insert(r.id).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate register storage"});if(!r.bank.empty())banks.insert(r.bank);for(size_t k=0;k<r.slices.size();++k){auto& s=r.slices[k];if(!s.width||uint64_t(s.begin)+s.width>uint64_t(UINT32_MAX)+1)return Result<int>::err({Error::Code::InvalidArgument,"invalid register storage lane"});for(size_t j=0;j<k;++j){auto& t=r.slices[j];if(s.unit==t.unit&&uint64_t(s.begin)<uint64_t(t.begin)+t.width&&uint64_t(t.begin)<uint64_t(s.begin)+s.width)return Result<int>::err({Error::Code::InvalidArgument,"overlapping slices within a register"});}}}
  for(auto reg:p.reserved)if(!physical.contains(reg))return Result<int>::err({Error::Code::InvalidArgument,"unknown reserved register"});
  for(auto& c:p.clobbers)for(auto x:c.registers)if(!physical.contains(x))return Result<int>::err({Error::Code::InvalidArgument,"unknown clobbered register"});
  std::unordered_set<VReg> seen;
  for(auto& r:p.ranges) {
    if(r.begin>r.end||!seen.insert(r.value).second)return Result<int>::err({Error::Code::InvalidArgument,"invalid live range or duplicate virtual register"});
    if(!classes.contains(r.klass))return Result<int>::err({Error::Code::InvalidArgument,"unknown register class: "+r.klass});
    if(!r.constraint.bank.empty()&&!banks.contains(r.constraint.bank))return Result<int>::err({Error::Code::InvalidArgument,"unknown register bank: "+r.constraint.bank});
    for(auto x:r.constraint.allowed)if(!physical.contains(x))return Result<int>::err({Error::Code::InvalidArgument,"unknown allowed register"});
    for(auto x:r.constraint.forbidden)if(!physical.contains(x))return Result<int>::err({Error::Code::InvalidArgument,"unknown forbidden register"});
    if(r.constraint.fixed&&legal(p,r).empty())return Result<int>::err({Error::Code::Unsatisfiable,"fixed register violates class, operand, or call constraints"});
  }
  for(auto [a,b]:p.interference)if(a==b||!seen.contains(a)||!seen.contains(b))return Result<int>::err({Error::Code::InvalidArgument,"invalid interference edge"});
  for(auto [a,b]:p.ties)if(!seen.contains(a)||!seen.contains(b))return Result<int>::err({Error::Code::InvalidArgument,"unknown tied value"});
  for(auto& tuple:p.tuples){std::unordered_set<VReg> members;if(tuple.values.size()<2||tuple.alternatives.empty())return Result<int>::err({Error::Code::InvalidArgument,"empty register tuple contract"});for(auto value:tuple.values)if(!seen.contains(value)||!members.insert(value).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate tuple value"});for(auto& alternative:tuple.alternatives){if(alternative.size()!=tuple.values.size())return Result<int>::err({Error::Code::InvalidArgument,"tuple alternative arity mismatch"});for(auto reg:alternative)if(!physical.contains(reg))return Result<int>::err({Error::Code::InvalidArgument,"unknown tuple register"});}}
  return Result<int>::ok(0);
}
Result<Allocation> linear_scan(const Program& p) {
  if(!p.ties.empty()||!p.tuples.empty())return graph_color(p);
  std::vector<const LiveRange*> order;for(auto& r:p.ranges)order.push_back(&r);
  std::sort(order.begin(),order.end(),[](auto a,auto b){return std::tie(a->begin,a->value)<std::tie(b->begin,b->value);});
  return assign(p,std::move(order));
}
Result<Allocation> greedy(const Program& p) {
  if(!p.ties.empty()||!p.tuples.empty())return graph_color(p);
  std::vector<const LiveRange*> order;for(auto& r:p.ranges)order.push_back(&r);
   auto degree=[&](const LiveRange* r){return std::count_if(p.ranges.begin(),p.ranges.end(),[&](auto& other){return other.value!=r->value&&overlaps(p,*r,other);});};
  std::sort(order.begin(),order.end(),[&](auto a,auto b){
    if(bool(a->constraint.fixed)!=bool(b->constraint.fixed))return bool(a->constraint.fixed);
    if(a->spillable!=b->spillable)return !a->spillable;
    auto da=degree(a),db=degree(b);return da!=db?da>db:a->value<b->value;
  });
  return assign(p,std::move(order));
}
Result<int> verify(const Program& p,const Allocation& a) {
  auto valid=validate(p);if(!valid)return valid;
  std::unordered_set<VReg> values,spilled;
  for(auto& r:p.ranges)values.insert(r.value);
  for(auto v:a.spilled)if(!values.contains(v)||!spilled.insert(v).second||a.regs.contains(v))return Result<int>::err({Error::Code::Conflict,"invalid spill assignment"});
  for(auto [v,reg]:a.regs)if(!values.contains(v))return Result<int>::err({Error::Code::Conflict,"assignment references unknown virtual register"});
  for(auto& r:p.ranges) {
    if(spilled.contains(r.value)) {
      if(!r.spillable||r.constraint.fixed)return Result<int>::err({Error::Code::Conflict,"nonspillable value spilled"});
      continue;
    }
    auto it=a.regs.find(r.value);
    if(it==a.regs.end())return Result<int>::err({Error::Code::Conflict,"unassigned value"});
    auto allowed=legal(p,r);
    if(std::find(allowed.begin(),allowed.end(),it->second)==allowed.end())return Result<int>::err({Error::Code::Conflict,"register class, operand, or call-clobber violation"});
  }
  auto conflict=[&](VReg x,VReg y){return a.regs.contains(x)&&a.regs.contains(y)&&aliases(p,a.regs.at(x),a.regs.at(y));};
  if(p.explicit_interference){for(auto [x,y]:p.interference)if(conflict(x,y))return Result<int>::err({Error::Code::Conflict,"register interference or aliasing violation"});}
  else for(size_t i=0;i<p.ranges.size();++i)for(size_t j=i+1;j<p.ranges.size();++j)if(overlaps(p,p.ranges[i],p.ranges[j])&&conflict(p.ranges[i].value,p.ranges[j].value))return Result<int>::err({Error::Code::Conflict,"register interference or aliasing violation"});
  for(auto [x,y]:p.ties)if(!a.regs.contains(x)||!a.regs.contains(y)||a.regs.at(x)!=a.regs.at(y))return Result<int>::err({Error::Code::Conflict,"broken tied operands"});
  for(auto& tuple:p.tuples){bool accepted=false;for(auto& alternative:tuple.alternatives){bool match=true;for(size_t k=0;k<tuple.values.size();++k)match&=a.regs.contains(tuple.values[k])&&a.regs.at(tuple.values[k])==alternative[k];accepted|=match;}if(!accepted)return Result<int>::err({Error::Code::Conflict,"register tuple violation"});}
  return Result<int>::ok(0);
}
std::string print(const Program& p) {
  std::string s="RegTL (inclusive live ranges)\n";
  auto ranges=p.ranges;std::sort(ranges.begin(),ranges.end(),[](auto& a,auto& b){return a.value<b.value;});
  for(auto& r:ranges)s+="v"+std::to_string(r.value)+" ["+std::to_string(r.begin)+","+std::to_string(r.end)+"] "+r.klass+"\n";
  return s;
}
}
