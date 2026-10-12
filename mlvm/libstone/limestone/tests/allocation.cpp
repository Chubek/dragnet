#include "test.hpp"
#include "regtl.hpp"
#include <limits>
#include <cmath>

int main(){return test_main([]{
  using namespace limestone;using namespace regtl;
  Program p{{{1,0,5,"G",{},true},{2,2,3,"G",{{},{},0},false}},{{"G",{0,1}}}};
  for(auto allocator:{linear_scan,greedy}) {
    auto a=take(allocator(p));take(verify(p,a));CHECK(a.regs.at(1)==1&&a.regs.at(2)==0);
    auto calls=p;calls.clobbers={{1,{1}}};auto c=take(allocator(calls));CHECK(c.spilled==std::vector<uint32_t>{1});take(verify(calls,c));
    Program pressure{{{1,0,3,"G",{},true},{2,1,2,"G",{},false}},{{"G",{0}}}};
    auto b=take(allocator(pressure));CHECK(b.regs.at(2)==0&&b.spilled==std::vector<uint32_t>{1});take(verify(pressure,b));
    Program overlap{{{1,0,1,"G",{},false},{2,1,2,"G",{},false}},{{"G",{0}}}};fails(allocator(overlap),Error::Code::Unsatisfiable);
    Program alias{{{1,0,1,"G",{{1},{},{}},false},{2,0,1,"G",{{2},{},{}},false}},{{"G",{0,1,2}}},{{0,1},{0,2}}};
    take(verify(alias,take(allocator(alias)))); // Overlap is not transitive.
    alias.aliases.push_back({1,2});fails(allocator(alias),Error::Code::Unsatisfiable);
  }
  auto bad=p;bad.ranges[0].end=0;bad.ranges[0].begin=1;fails(validate(bad),Error::Code::InvalidArgument);
  bad=p;bad.ranges[1].constraint.forbidden={0};fails(validate(bad),Error::Code::Unsatisfiable);
  bad=p;bad.ranges[1].constraint.fixed=99;fails(validate(bad),Error::Code::Unsatisfiable);
  auto valid=take(linear_scan(p));auto wrong=valid;wrong.regs[2]=1;fails(verify(p,wrong),Error::Code::Conflict);
  wrong=valid;wrong.spilled={99};fails(verify(p,wrong),Error::Code::Conflict);
  wrong=valid;wrong.spilled={2};wrong.regs.erase(2);fails(verify(p,wrong),Error::Code::Conflict);
  wrong=valid;wrong.regs[99]=0;fails(verify(p,wrong),Error::Code::Conflict);
  Program lanes{{{1,0,5,"G",{{1},{},{}},false},{2,0,5,"G",{{2},{},{}},false}},{{"G",{0,1,2,3}}}};
  lanes.storage={{0,"A",{{10,0,64}}},{1,"A",{{10,0,32}}},{2,"A",{{10,32,32}}},{3,"B",{{11,0,64}}}};
  for(auto allocator:{linear_scan,greedy,graph_color}){auto a=take(allocator(lanes));CHECK(a.regs.at(1)==1&&a.regs.at(2)==2);take(verify(lanes,a));}
  auto full=lanes;full.ranges[0].constraint.allowed={0};fails(graph_color(full),Error::Code::Unsatisfiable);
  auto banks=lanes;banks.ranges[0].constraint.allowed.clear();banks.ranges[0].constraint.bank="B";CHECK(take(greedy(banks)).regs.at(1)==3);banks.ranges[0].constraint.bank="missing";fails(validate(banks),Error::Code::InvalidArgument);
  lanes.ranges[0].constraint.allowed.clear();lanes.ranges[1].constraint.allowed.clear();lanes.tuples={{{1,2},{{1,2},{2,1}}}};
  auto tuple=take(constraint_allocate(lanes));CHECK((tuple.regs.at(1)==1&&tuple.regs.at(2)==2)||(tuple.regs.at(1)==2&&tuple.regs.at(2)==1));take(verify(lanes,tuple));
  auto invalid_tuple=tuple;invalid_tuple.regs[1]=3;fails(verify(lanes,invalid_tuple),Error::Code::Conflict);lanes.ranges[0].constraint.fixed=0;fails(constraint_allocate(lanes),Error::Code::Unsatisfiable);
  auto invalid_lane=full;invalid_lane.storage[0].slices[0].width=0;fails(validate(invalid_lane),Error::Code::InvalidArgument);

  // Spill and unary costs change the optimum; interference is a hard matrix.
  Program weighted{{{1,0,5,"G"},{2,0,5,"G"}},{{"G",{0}}}};
  PbqpOptions costs;costs.costs.values={{1,9,{}},{2,2,{}}};
  auto optimum=take(solve_pbqp(weighted,costs));CHECK(optimum.cost==2&&optimum.allocation.spilled==std::vector<VReg>{2});CHECK(optimum.statistics.degree_one==1&&optimum.statistics.degree_zero==1);
  costs.costs.values[0].registers={{0,20}};optimum=take(solve_pbqp(weighted,costs));CHECK(optimum.cost==9&&optimum.allocation.spilled==std::vector<VReg>{1});
  Program moves{{{1,0,1,"G",{},false},{2,2,3,"G",{},false}},{{"G",{0,1}}}};
  costs={};costs.costs.values={{1,1,{{0,0},{1,3}}},{2,1,{{0,2},{1,0}}}};costs.costs.coalescing={{1,2,5}};
  optimum=take(solve_pbqp(moves,costs));CHECK(optimum.cost==2&&optimum.allocation.regs.at(1)==0&&optimum.allocation.regs.at(2)==0);
  auto permuted=moves;std::reverse(permuted.ranges.begin(),permuted.ranges.end());std::reverse(permuted.classes[0].members.begin(),permuted.classes[0].members.end());CHECK(take(pbqp_allocate(permuted,costs)).regs==optimum.allocation.regs);
  Program triangle{{{1,0,5,"G"},{2,0,5,"G"},{3,0,5,"G"}},{{"G",{0,1}}}};
  optimum=take(solve_pbqp(triangle));CHECK(optimum.cost==1&&optimum.allocation.spilled.size()==1&&optimum.statistics.degree_two==1);
  auto clique=triangle;clique.ranges.push_back({4,0,5,"G"});optimum=take(solve_pbqp(clique));CHECK(optimum.cost==2&&optimum.statistics.residual_variables==4&&optimum.statistics.search_steps>0);
  PbqpOptions bounded;bounded.search_limit=0;fails(solve_pbqp(clique,bounded),Error::Code::ResourceLimit);take(solve_pbqp(triangle,bounded));
  bounded={};bounded.cell_limit=1;fails(solve_pbqp(weighted,bounded),Error::Code::ResourceLimit);bounded={};bounded.work_limit=0;fails(solve_pbqp(weighted,bounded),Error::Code::ResourceLimit);
  for(auto& range:clique.ranges)range.spillable=false;fails(solve_pbqp(clique),Error::Code::Unsatisfiable);
  lanes.ranges[0].constraint.fixed.reset();take(verify(lanes,take(pbqp_allocate(lanes))));auto split_lanes=full;split_lanes.ranges[0].constraint.allowed={1};take(verify(split_lanes,take(pbqp_allocate(split_lanes))));fails(pbqp_allocate(full),Error::Code::Unsatisfiable);
  auto tied=moves;tied.ties={{1,2}};auto tie=take(pbqp_allocate(tied));CHECK(tie.regs.at(1)==tie.regs.at(2));tied.explicit_interference=true;tied.interference={{1,2}};fails(pbqp_allocate(tied),Error::Code::Unsatisfiable);
  auto bad_costs=costs;bad_costs.costs.values[0].spill_cost=-1;fails(solve_pbqp(moves,bad_costs),Error::Code::InvalidArgument);bad_costs=costs;bad_costs.costs.values[0].spill_cost=std::numeric_limits<double>::quiet_NaN();fails(solve_pbqp(moves,bad_costs),Error::Code::InvalidArgument);
  bad_costs=costs;bad_costs.costs.values[0].value=99;fails(solve_pbqp(moves,bad_costs),Error::Code::InvalidArgument);bad_costs=costs;bad_costs.costs.values[0].registers.push_back({0,1});fails(solve_pbqp(moves,bad_costs),Error::Code::InvalidArgument);
  bad_costs=costs;bad_costs.costs.coalescing.push_back({2,1,1});fails(solve_pbqp(moves,bad_costs),Error::Code::InvalidArgument);
  auto overflow=moves;bad_costs={};bad_costs.costs.values={{1,1,{{0,1e308},{1,1e308}}},{2,1,{{0,1e308},{1,1e308}}}};fails(solve_pbqp(overflow,bad_costs),Error::Code::ResourceLimit);
  Program chain;chain.classes={{"G",{0,1}}};chain.explicit_interference=true;
  for(VReg i=0;i<1500;++i){chain.ranges.push_back({i,0,1,"G",{},false});if(i)chain.interference.emplace_back(i-1,i);}
  bounded={};bounded.search_limit=0;optimum=take(solve_pbqp(chain,bounded));CHECK(optimum.cost==0&&optimum.statistics.degree_one==1499&&!optimum.statistics.residual_variables);take(verify(chain,optimum.allocation));

  // Exhaustive independent enumeration establishes the optimum for small
  // irregular graphs, including lane aliases, tuples, and coalescing cycles.
  uint32_t seed=91;auto random=[&]{seed=seed*1664525+1013904223;return seed;};
  for(size_t trial=0;trial<120;++trial) {
    Program problem;problem.classes={{"G",{0,1,2}}};problem.explicit_interference=true;
    PbqpOptions policy;size_t count=2+random()%4;
    for(VReg i=0;i<count;++i){problem.ranges.push_back({i,0,1,"G"});policy.costs.values.push_back({i,double(1+random()%8),{{0,double(random()%4)},{1,double(random()%4)},{2,double(random()%4)}}});}
    for(VReg i=0;i<count;++i)for(VReg j=i+1;j<count;++j){if(random()%3)problem.interference.emplace_back(i,j);if(random()%3==0)policy.costs.coalescing.push_back({i,j,double(random()%5)});}
    if(trial%3==0)problem.aliases={{0,1}};
    if(trial%4==0)problem.tuples={{{0,1},{{0,2},{2,0},{1,2}}}};
    double reference=std::numeric_limits<double>::infinity();size_t combinations=size_t(1)<<(2*count);
    for(size_t code=0;code<combinations;++code) {
      Allocation candidate;double total=0;std::vector<size_t> choices(count);auto digits=code;
      for(VReg i=0;i<count;++i){choices[i]=digits%4;digits/=4;auto& unary=policy.costs.values[i];if(choices[i]==3){candidate.spilled.push_back(i);total+=unary.spill_cost;}else {candidate.regs[i]=PReg(choices[i]);total+=unary.registers[choices[i]].second;}}
      for(auto& move:policy.costs.coalescing)if(choices[move.first]==3||choices[move.second]==3||choices[move.first]!=choices[move.second])total+=move.cost;
      if(verify(problem,candidate))reference=std::min(reference,total);
    }
    auto solved=solve_pbqp(problem,policy);if(std::isfinite(reference)){auto result=take(std::move(solved));CHECK(result.cost==reference);take(verify(problem,result.allocation));}else fails(solved,Error::Code::Unsatisfiable);
  }
});}
