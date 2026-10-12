#include "regtl.hpp"
#include <map>
#include <set>

namespace limestone::regtl {
namespace {
struct Group { std::vector<VReg> values; std::vector<PReg> allowed; bool spillable=true; std::set<size_t> neighbors; };
bool alias(const Program& p,PReg a,PReg b) {
  return registers_overlap(p,a,b);
}
Result<std::vector<Group>> groups(const Program& p) {
  auto valid=validate(p);if(!valid)return Result<std::vector<Group>>::err(valid.error());
  std::map<VReg,VReg> parent;for(auto& r:p.ranges)parent[r.value]=r.value;
  std::function<VReg(VReg)> root=[&](VReg value){return parent[value]==value?value:parent[value]=root(parent[value]);};
  for(auto [a,b]:p.ties){auto x=root(a),y=root(b);parent[std::max(x,y)]=std::min(x,y);}
  std::map<VReg,size_t> index;std::vector<Group> result;
  auto ranges=p.ranges;std::sort(ranges.begin(),ranges.end(),[](auto& a,auto& b){return a.value<b.value;});
  for(auto& r:ranges) {
    auto leader=root(r.value);auto [it,added]=index.emplace(leader,result.size());if(added)result.emplace_back();auto& g=result[it->second];
    auto allowed=allowed_registers(p,r);
    if(g.values.empty())g.allowed=allowed;
    else {std::vector<PReg> intersection;std::set_intersection(g.allowed.begin(),g.allowed.end(),allowed.begin(),allowed.end(),std::back_inserter(intersection));g.allowed=std::move(intersection);}
    g.values.push_back(r.value);g.spillable&=r.spillable&&!r.constraint.fixed;
  }
  for(auto [a,b]:p.ties){result[index.at(root(a))].spillable=false;}
  for(auto& tuple:p.tuples)for(auto value:tuple.values)result[index.at(root(value))].spillable=false;
  for(size_t i=0;i<ranges.size();++i)for(size_t j=i+1;j<ranges.size();++j) {
    auto& a=ranges[i];auto& b=ranges[j];bool interfere=a.begin<=b.end&&b.begin<=a.end;
    if(p.explicit_interference)interfere=std::any_of(p.interference.begin(),p.interference.end(),[&](auto e){return e==std::pair{a.value,b.value}||e==std::pair{b.value,a.value};});
    if(!interfere)continue;auto x=index.at(root(a.value)),y=index.at(root(b.value));
    if(x==y)return Result<std::vector<Group>>::err({Error::Code::Unsatisfiable,"tied operands have interfering lifetimes"});
    result[x].neighbors.insert(y);result[y].neighbors.insert(x);
  }
  return Result<std::vector<Group>>::ok(std::move(result));
}
Result<Allocation> color(const Program& p,size_t budget,bool exact) {
  auto grouped=groups(p);if(!grouped)return Result<Allocation>::err(grouped.error());auto& gs=grouped.value();
  std::vector<std::optional<PReg>> assigned(gs.size());std::vector<bool> done(gs.size());size_t visits=0;bool exhausted=false;
  std::map<VReg,size_t> owners;for(size_t k=0;k<gs.size();++k)for(auto value:gs[k].values)owners[value]=k;
  auto tuples_fit=[&]{for(auto& tuple:p.tuples){bool feasible=false;for(auto& alternative:tuple.alternatives){bool matches=true;for(size_t k=0;k<tuple.values.size();++k){auto owner=owners.at(tuple.values[k]);matches&=!done[owner]||(assigned[owner]&&*assigned[owner]==alternative[k]);}feasible|=matches;}if(!feasible)return false;}return true;};
  std::function<bool(size_t)> search=[&](size_t depth) {
    if(depth==gs.size())return true;
    if(++visits>budget){exhausted=true;return false;}
    size_t best=gs.size();std::tuple<bool,size_t,size_t,size_t> score{};
    for(size_t k=0;k<gs.size();++k)if(!done[k]) {
      std::set<PReg> saturation;for(auto neighbor:gs[k].neighbors)if(assigned[neighbor])saturation.insert(*assigned[neighbor]);
      auto current=std::tuple{!gs[k].spillable,saturation.size(),gs[k].neighbors.size(),size_t(-1)-k};
      if(best==gs.size()||current>score){best=k;score=current;}
    }
    auto& g=gs[best];done[best]=true;
    for(auto reg:g.allowed) {
      bool busy=false;for(auto neighbor:g.neighbors)if(assigned[neighbor]&&alias(p,reg,*assigned[neighbor]))busy=true;
      if(busy)continue;assigned[best]=reg;
      if(tuples_fit()&&search(depth+1))return true;assigned[best].reset();if(exhausted)break;
      if(!exact)break;
    }
    if(!exhausted&&g.spillable&&search(depth+1))return true;
    done[best]=false;return false;
  };
  if(!search(0))return Result<Allocation>::err({exhausted?Error::Code::ResourceLimit:Error::Code::Unsatisfiable,exhausted?"allocation search budget exhausted":"no register assignment satisfies interference and ties"});
  Allocation a;
  for(size_t k=0;k<gs.size();++k)for(auto value:gs[k].values)if(assigned[k])a.regs[value]=*assigned[k];else a.spilled.push_back(value);
  std::sort(a.spilled.begin(),a.spilled.end());auto checked=verify(p,a);if(!checked)return Result<Allocation>::err(checked.error());
  return Result<Allocation>::ok(std::move(a));
}
}
Result<Allocation> graph_color(const Program& p){return color(p,1000000,true);}
Result<Allocation> constraint_allocate(const Program& p,size_t limit){return color(p,limit,true);}
}
