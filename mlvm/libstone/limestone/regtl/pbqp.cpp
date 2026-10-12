#include "regtl.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace limestone::regtl {
namespace {
constexpr double infinity=std::numeric_limits<double>::infinity();
constexpr size_t absent=std::numeric_limits<size_t>::max();
[[noreturn]] void limit(std::string_view what) { throw Error{Error::Code::ResourceLimit,"PBQP "+std::string(what)+" limit exceeded"}; }
double add(double a,double b) {
  if(a==infinity||b==infinity)return infinity;
  auto sum=a+b;if(!std::isfinite(sum))limit("cost arithmetic");return sum;
}
bool cost_valid(double cost) { return std::isfinite(cost)&&cost>=0; }
const UnaryCost* unary(const AllocationCosts& costs,VReg value) {
  auto found=std::find_if(costs.values.begin(),costs.values.end(),[&](auto& c){return c.value==value;});
  return found==costs.values.end()?nullptr:&*found;
}
double reg_cost(const UnaryCost* costs,PReg reg) {
  if(costs)for(auto [physical,cost]:costs->registers)if(physical==reg)return cost;
  return 0;
}
struct Node {
  std::vector<std::optional<PReg>> locations;
  std::vector<double> costs;
  std::set<size_t> neighbors;
  bool active=true;
};
struct Matrix { size_t columns; std::vector<double> costs; };
struct Reduction { size_t node; std::vector<size_t> neighbors,choices; };
struct Solver {
  const Program& program;
  const PbqpOptions& options;
  PbqpStatistics stats;
  std::vector<Node> nodes;
  std::map<std::pair<size_t,size_t>,Matrix> edges;
  std::vector<Reduction> reductions;
  std::map<VReg,size_t> values;
  std::map<std::pair<PReg,PReg>,bool> overlaps;

  void work(size_t count=1) {
    if(count>options.work_limit-stats.work)limit("work");stats.work+=count;
  }
  size_t cells(size_t rows,size_t columns=1) {
    if(rows&&columns>(options.cell_limit-stats.cells)/rows)limit("matrix/reconstruction cell");
    auto count=rows*columns;stats.cells+=count;return count;
  }
  Matrix& edge(size_t a,size_t b) {
    if(a>b)std::swap(a,b);
    auto key=std::pair{a,b};auto found=edges.find(key);if(found!=edges.end())return found->second;
    auto count=cells(nodes[a].costs.size(),nodes[b].costs.size());
    auto& result=edges.emplace(key,Matrix{nodes[b].costs.size(),std::vector<double>(count)}).first->second;
    nodes[a].neighbors.insert(b);nodes[b].neighbors.insert(a);return result;
  }
  double pair_cost(size_t a,size_t x,size_t b,size_t y) const {
    if(a>b){std::swap(a,b);std::swap(x,y);}
    auto found=edges.find({a,b});return found==edges.end()?0:found->second.costs[x*found->second.columns+y];
  }
  template<class F> void constrain(size_t a,size_t b,F cost) {
    if(a==b)throw Error{Error::Code::Internal,"PBQP self constraint"};
    bool transposed=a>b;auto& matrix=edge(a,b);
    for(size_t x=0;x<nodes[a].costs.size();++x)for(size_t y=0;y<nodes[b].costs.size();++y) {
      work();auto index=transposed?y*matrix.columns+x:x*matrix.columns+y;
      matrix.costs[index]=add(matrix.costs[index],cost(x,y));
    }
  }
  bool overlap(PReg a,PReg b) {
    auto key=std::minmax(a,b);auto [it,inserted]=overlaps.emplace(std::pair{key.first,key.second},false);
    if(inserted){work();it->second=registers_overlap(program,a,b);}return it->second;
  }
  void build() {
    std::set<VReg> protected_values;
    for(auto [a,b]:program.ties){protected_values.insert(a);protected_values.insert(b);}
    for(auto& tuple:program.tuples)protected_values.insert(tuple.values.begin(),tuple.values.end());
    auto ranges=program.ranges;std::sort(ranges.begin(),ranges.end(),[](auto& a,auto& b){return a.value<b.value;});
    for(auto& range:ranges) {
      Node node;auto policy=unary(options.costs,range.value);
      for(auto reg:allowed_registers(program,range)){node.locations.push_back(reg);node.costs.push_back(reg_cost(policy,reg));}
      if(range.spillable&&!range.constraint.fixed&&!protected_values.contains(range.value)) {
        node.locations.push_back({});node.costs.push_back(policy?policy->spill_cost:options.costs.default_spill_cost);
      }
      if(node.costs.empty())throw Error{Error::Code::Unsatisfiable,"PBQP: no legal location for v"+std::to_string(range.value)};
      work(node.costs.size());cells(node.costs.size());values.emplace(range.value,nodes.size());nodes.push_back(std::move(node));
    }
    auto interfere=[&](VReg a,VReg b) {
      auto x=values.at(a),y=values.at(b);
      constrain(x,y,[&](size_t i,size_t j){auto a=nodes[x].locations[i],b=nodes[y].locations[j];return a&&b&&overlap(*a,*b)?infinity:0;});
    };
    if(program.explicit_interference) {
      std::set<std::pair<VReg,VReg>> unique;
      for(auto [a,b]:program.interference)if(unique.emplace(std::min(a,b),std::max(a,b)).second)interfere(a,b);
    }else for(size_t i=0;i<ranges.size();++i)for(size_t j=i+1;j<ranges.size();++j) {
      work();if(ranges[i].begin<=ranges[j].end&&ranges[j].begin<=ranges[i].end)interfere(ranges[i].value,ranges[j].value);
    }
    for(auto [a,b]:program.ties)if(a!=b) {
      auto x=values.at(a),y=values.at(b);
      constrain(x,y,[&](size_t i,size_t j){return nodes[x].locations[i]==nodes[y].locations[j]?0:infinity;});
    }
    for(auto& move:options.costs.coalescing) {
      auto x=values.at(move.first),y=values.at(move.second);
      constrain(x,y,[&](size_t i,size_t j){auto a=nodes[x].locations[i],b=nodes[y].locations[j];return a&&b&&*a==*b?0:move.cost;});
    }
    for(auto& tuple:program.tuples) {
      Node node;cells(tuple.alternatives.size());node.costs.resize(tuple.alternatives.size());auto auxiliary=nodes.size();nodes.push_back(std::move(node));
      for(size_t k=0;k<tuple.values.size();++k) {
        auto value=values.at(tuple.values[k]);
        constrain(value,auxiliary,[&](size_t i,size_t j){return nodes[value].locations[i]==tuple.alternatives[j][k]?0:infinity;});
      }
    }
  }
  void reduce(size_t id) {
    auto& node=nodes[id];Reduction reduction{id,{node.neighbors.begin(),node.neighbors.end()},{}};
    auto degree=reduction.neighbors.size();auto rows=degree?nodes[reduction.neighbors[0]].costs.size():1;
    auto columns=degree==2?nodes[reduction.neighbors[1]].costs.size():1;
    reduction.choices.resize(cells(rows,columns),absent);
    Matrix* matrix=degree==2?&edge(reduction.neighbors[0],reduction.neighbors[1]):nullptr;
    for(size_t a=0;a<rows;++a)for(size_t b=0;b<columns;++b) {
      double best=infinity;size_t choice=absent;
      for(size_t x=0;x<node.costs.size();++x) {
        work();auto cost=node.costs[x];
        if(degree)cost=add(cost,pair_cost(id,x,reduction.neighbors[0],a));
        if(degree==2)cost=add(cost,pair_cost(id,x,reduction.neighbors[1],b));
        if(cost<best){best=cost;choice=x;}
      }
      reduction.choices[a*columns+b]=choice;
      if(!degree&&choice==absent)throw Error{Error::Code::Unsatisfiable,"PBQP: constraints have no finite solution"};
      if(degree==1){auto& cost=nodes[reduction.neighbors[0]].costs[a];cost=add(cost,best);}
      if(degree==2){auto& cost=matrix->costs[a*columns+b];cost=add(cost,best);}
    }
    for(auto neighbor:reduction.neighbors){nodes[neighbor].neighbors.erase(id);edges.erase({std::min(id,neighbor),std::max(id,neighbor)});}
    node.neighbors.clear();node.active=false;reductions.push_back(std::move(reduction));
    if(!degree)++stats.degree_zero;else if(degree==1)++stats.degree_one;else ++stats.degree_two;
  }
  std::vector<size_t> solve() {
    // Stable IDs determine elimination and equal-cost choices. Fill edges never
    // change domains; the stored argmins reconstruct every eliminated variable.
    std::set<size_t> ready;
    for(size_t i=0;i<nodes.size();++i)if(nodes[i].neighbors.size()<=2)ready.insert(i);
    while(!ready.empty()) {
      auto id=*ready.begin();ready.erase(ready.begin());if(!nodes[id].active||nodes[id].neighbors.size()>2)continue;
      auto neighbors=nodes[id].neighbors;reduce(id);
      for(auto neighbor:neighbors)if(nodes[neighbor].active&&nodes[neighbor].neighbors.size()<=2)ready.insert(neighbor);
    }
    std::vector<size_t> residual;
    for(size_t i=0;i<nodes.size();++i)if(nodes[i].active)residual.push_back(i);
    stats.residual_variables=residual.size();
    std::sort(residual.begin(),residual.end(),[&](size_t a,size_t b){return std::tuple{nodes[a].costs.size(),size_t(-1)-nodes[a].neighbors.size(),a}<std::tuple{nodes[b].costs.size(),size_t(-1)-nodes[b].neighbors.size(),b};});
    std::vector<size_t> chosen(nodes.size(),absent),best_choices,next(residual.size());
    std::vector<double> partial(residual.size()+1);
    double best=infinity;size_t depth=0;
    if(residual.empty())best_choices=chosen;
    else while(true) {
      if(depth==residual.size()) {
        if(partial[depth]<best){best=partial[depth];best_choices=chosen;}
        --depth;chosen[residual[depth]]=absent;continue;
      }
      auto id=residual[depth];
      if(next[depth]==nodes[id].costs.size()) {
        next[depth]=0;chosen[id]=absent;if(!depth)break;--depth;chosen[residual[depth]]=absent;continue;
      }
      if(stats.search_steps==options.search_limit)limit("residual search");++stats.search_steps;work();
      auto option=next[depth]++;auto cost=add(partial[depth],nodes[id].costs[option]);
      for(auto neighbor:nodes[id].neighbors)if(chosen[neighbor]!=absent)cost=add(cost,pair_cost(id,option,neighbor,chosen[neighbor]));
      if(cost==infinity||cost>=best)continue;
      chosen[id]=option;
      // A valid lower bound: independent minimum unary plus edges to assigned
      // nodes. Unassigned pair costs are nonnegative and can be omitted.
      double bound=cost;
      for(size_t k=depth+1;k<residual.size();++k) {
        auto other=residual[k];double minimum=infinity;
        for(size_t x=0;x<nodes[other].costs.size();++x) {
          work();auto value=nodes[other].costs[x];
          for(auto neighbor:nodes[other].neighbors)if(chosen[neighbor]!=absent)value=add(value,pair_cost(other,x,neighbor,chosen[neighbor]));
          minimum=std::min(minimum,value);
        }
        bound=add(bound,minimum);
      }
      if(bound<best){partial[depth+1]=cost;++depth;}else chosen[id]=absent;
    }
    if(best_choices.empty()&&!nodes.empty())throw Error{Error::Code::Unsatisfiable,"PBQP: constraints have no finite solution"};
    for(auto it=reductions.rbegin();it!=reductions.rend();++it) {
      size_t index=0;
      if(!it->neighbors.empty())index=best_choices[it->neighbors[0]];
      if(it->neighbors.size()==2)index=index*nodes[it->neighbors[1]].costs.size()+best_choices[it->neighbors[1]];
      auto option=it->choices.at(index);if(option==absent)throw Error{Error::Code::Internal,"PBQP reconstruction has no finite argmin"};
      best_choices[it->node]=option;
    }
    return best_choices;
  }
};
}

Result<int> validate_costs(const Program& program,const AllocationCosts& costs) {
  if(!cost_valid(costs.default_spill_cost))return Result<int>::err({Error::Code::InvalidArgument,"allocation spill cost must be finite and nonnegative"});
  std::set<VReg> values,policies;std::set<PReg> physical;
  for(auto& range:program.ranges)values.insert(range.value);
  for(auto& klass:program.classes)physical.insert(klass.members.begin(),klass.members.end());
  for(auto& policy:costs.values) {
    if(!values.contains(policy.value)||!policies.insert(policy.value).second||!cost_valid(policy.spill_cost))return Result<int>::err({Error::Code::InvalidArgument,"unknown/duplicate allocation cost value or invalid spill cost"});
    std::set<PReg> registers;
    for(auto [reg,cost]:policy.registers)if(!physical.contains(reg)||!registers.insert(reg).second||!cost_valid(cost))return Result<int>::err({Error::Code::InvalidArgument,"unknown/duplicate physical cost or invalid register cost"});
  }
  std::set<std::pair<VReg,VReg>> moves;
  for(auto& move:costs.coalescing)if(move.first==move.second||!values.contains(move.first)||!values.contains(move.second)||!cost_valid(move.cost)||!moves.emplace(std::min(move.first,move.second),std::max(move.first,move.second)).second)return Result<int>::err({Error::Code::InvalidArgument,"invalid/duplicate coalescing cost"});
  return Result<int>::ok(0);
}
Result<double> allocation_cost(const Program& program,const Allocation& allocation,const AllocationCosts& costs) {
  auto valid=verify(program,allocation);if(!valid)return Result<double>::err(valid.error());
  valid=validate_costs(program,costs);if(!valid)return Result<double>::err(valid.error());
  try {
    double result=0;auto ranges=program.ranges;std::sort(ranges.begin(),ranges.end(),[](auto& a,auto& b){return a.value<b.value;});
    for(auto& range:ranges){auto policy=unary(costs,range.value);auto reg=allocation.regs.find(range.value);result=add(result,reg==allocation.regs.end()?(policy?policy->spill_cost:costs.default_spill_cost):reg_cost(policy,reg->second));}
    for(auto& move:costs.coalescing)if(!allocation.regs.contains(move.first)||!allocation.regs.contains(move.second)||allocation.regs.at(move.first)!=allocation.regs.at(move.second))result=add(result,move.cost);
    return Result<double>::ok(result);
  }catch(const Error& error){return Result<double>::err(error);}
}
Result<PbqpSolution> solve_pbqp(const Program& program,const PbqpOptions& options) {
  auto valid=validate(program);if(!valid)return Result<PbqpSolution>::err(valid.error());
  valid=validate_costs(program,options.costs);if(!valid)return Result<PbqpSolution>::err(valid.error());
  try {
    Solver solver{program,options};solver.build();auto choices=solver.solve();PbqpSolution result;
    for(auto [value,id]:solver.values){auto location=solver.nodes[id].locations.at(choices.at(id));if(location)result.allocation.regs[value]=*location;else result.allocation.spilled.push_back(value);}
    auto cost=allocation_cost(program,result.allocation,options.costs);if(!cost)return Result<PbqpSolution>::err(cost.error());
    result.cost=cost.value();result.statistics=solver.stats;return Result<PbqpSolution>::ok(std::move(result));
  }catch(const Error& error){return Result<PbqpSolution>::err(error);}
  catch(const std::bad_alloc&){return Result<PbqpSolution>::err({Error::Code::ResourceLimit,"PBQP allocation failed"});}
}
Result<Allocation> pbqp_allocate(const Program& program,const PbqpOptions& options) {
  auto result=solve_pbqp(program,options);if(!result)return Result<Allocation>::err(result.error());
  return Result<Allocation>::ok(std::move(result.value().allocation));
}
}
