#include "test.hpp"
#include "unisel.hpp"
#include "limeburg.hpp"
#include <set>

int main(){return test_main([]{
  using namespace limestone;
  unisel::Program p{{{1,"const",{},100,"i64"},{2,"const",{},7,"i64"},{3,"add",{1,2},{},"i64"}},{3}};
  unisel::Pattern fused{3,"immediate","add","ADDI",{},6};
  fused.tree=unisel::PatternTree{"add","","i64",{{"","lhs","i64"},{"const","","i64",{},std::pair<int64_t,int64_t>{-8,7}}}};
  std::vector<unisel::Pattern> patterns{{1,"constant","const","CONST",{},0},{2,"add","add","ADD",{"v","v"},7},fused};
  auto model=take(unisel::build_model(p,patterns));CHECK(model.candidates.size()==4);
  auto solved=take(unisel::solve(p,patterns));CHECK(solved.cost==6&&solved.selected.size()==2);
  CHECK(take(unisel::solve_greedy(p,patterns)).cost==7);
  auto emitted=take(unisel::emit_scheduler(p,patterns,solved));CHECK(emitted.instructions.size()==2);
  CHECK(emitted.instructions[0].id==1&&emitted.instructions[0].immediates==std::vector<std::pair<uint32_t,int64_t>>{{1,100}});
  CHECK(emitted.instructions[1].opcode=="ADDI"&&emitted.instructions[1].uses==std::vector<uint32_t>{1});
  CHECK(emitted.instructions[1].immediates==std::vector<std::pair<uint32_t,int64_t>>{{2,7}});
  auto schedule=take(schedrow::schedule(emitted,{}));CHECK(schedule.size()==2);take(schedrow::verify(emitted,{},schedule));
  auto shared=p;shared.outputs.push_back(2);CHECK(take(unisel::solve(shared,patterns)).cost==7);
  auto wrong_type=p;wrong_type.nodes[1].type="f64";CHECK(take(unisel::solve(wrong_type,patterns)).cost==7);
  auto typed=patterns;typed[1].operands={"i64","i64"};
  CHECK(take(unisel::solve(p,typed)).cost==6);
  fails(unisel::solve(wrong_type,typed),Error::Code::Unsatisfiable);
  fails(unisel::solve_greedy(wrong_type,typed),Error::Code::Unsatisfiable);
  auto out_of_range=p;out_of_range.nodes[1].constant=8;CHECK(take(unisel::solve(out_of_range,patterns)).cost==7);
  auto forged=solved;forged.selected[0].covered.clear();fails(unisel::emit_scheduler(p,patterns,forged),Error::Code::InvalidArgument);
  fails(unisel::solve(p,{patterns[0]}),Error::Code::Unsatisfiable);
  auto cycle=p;cycle.nodes[0].inputs={3};fails(unisel::solve(cycle,patterns),Error::Code::Conflict);
  auto missing=p;missing.nodes[2].inputs[0]=99;fails(unisel::solve(missing,patterns),Error::Code::InvalidArgument);
  auto effect=p;effect.nodes[2].side_effect=true;fails(unisel::solve(effect,patterns),Error::Code::Unsatisfiable);
  // Memory source order and dataflow must agree before matcher rejection becomes
  // a missing-coverage diagnosis. Preparation conflicts retain their error code.
  unisel::Program inverted_effects{{{1,"store",{2},{},"i64"},{2,"load",{}, {},"i64"}},{2}};
  inverted_effects.nodes[0].produces_value=false;inverted_effects.nodes[0].access=schedrow::MemoryAccess{false,true,false,false,schedrow::MemoryOrdering::Relaxed,"heap",{1}};
  inverted_effects.nodes[1].access=schedrow::MemoryAccess{true,false,false,false,schedrow::MemoryOrdering::Relaxed,"heap",{1}};
  std::vector<unisel::Pattern> effect_patterns{{1,"store","store","STORE",{"i64"},1},{2,"load","load","LOAD",{},1}};for(auto& pattern:effect_patterns)pattern.supports_side_effects=true;
  take(unisel::validate(inverted_effects,effect_patterns));fails(unisel::build_model(inverted_effects,effect_patterns),Error::Code::Conflict);
  fails(unisel::solve(inverted_effects,effect_patterns),Error::Code::Conflict);fails(unisel::solve_greedy(inverted_effects,effect_patterns),Error::Code::Conflict);
  std::reverse(inverted_effects.nodes.begin(),inverted_effects.nodes.end());CHECK(take(unisel::solve(inverted_effects,effect_patterns)).cost==2&&take(unisel::solve_greedy(inverted_effects,effect_patterns)).cost==2);
  unisel::Program repeated{{{1,"input",{}, {},"i64",0,false},{2,"add",{1,1},{},"i64"}},{2}};
  auto tied=fused;tied.id=9;tied.cost=1;tied.instruction="DOUBLE";
  tied.tree=unisel::PatternTree{"add","","i64",{{"","x","i64"},{"","x","i64"}}};
  CHECK(take(unisel::solve(repeated,{tied})).cost==1);
  repeated.nodes.insert(repeated.nodes.begin(),{0,"input",{},{},"i64",0,false});repeated.nodes.back().inputs[1]=0;
  fails(unisel::solve(repeated,{tied}),Error::Code::Unsatisfiable);
  // Exhaustive exact-cover reference exercises Boolean weighted-cost carries.
  for(int constant_cost=0;constant_cost<4;++constant_cost)for(int fused_cost=0;fused_cost<4;++fused_cost) {
    auto ps=patterns;ps[0].cost=constant_cost;ps[1].cost=3;ps[2].cost=fused_cost;
    auto candidates=unisel::match(p,ps);int best=100000;
    for(unsigned mask=0;mask<(1u<<candidates.size());++mask) {
      std::map<uint32_t,unsigned> coverage;int cost=0;
      for(size_t i=0;i<candidates.size();++i)if(mask&(1u<<i)){cost+=candidates[i].cost;for(auto id:candidates[i].covered)++coverage[id];}
      if(coverage[1]==1&&coverage[2]==1&&coverage[3]==1)best=std::min(best,cost);
    }
    CHECK(take(unisel::solve(p,ps)).cost==best);
  }
  // Multi-bit weights exercise the pseudo-Boolean cost-bound encoder, and
  // repeated solves must agree exactly (lexicographic optimum fixing).
  for(int constant_cost:{0,5,100})for(int fused_cost:{0,7,200}) {
    auto ps=patterns;ps[0].cost=constant_cost;ps[1].cost=17;ps[2].cost=fused_cost;
    auto candidates=unisel::match(p,ps);int best=1000000;
    for(unsigned mask=0;mask<(1u<<candidates.size());++mask) {
      std::map<uint32_t,unsigned> coverage;int cost=0;
      for(size_t i=0;i<candidates.size();++i)if(mask&(1u<<i)){cost+=candidates[i].cost;for(auto id:candidates[i].covered)++coverage[id];}
      if(coverage[1]==1&&coverage[2]==1&&coverage[3]==1)best=std::min(best,cost);
    }
    auto once=take(unisel::solve(p,ps));CHECK(once.cost==best);
    auto twice=take(unisel::solve(p,ps));CHECK(twice.cost==best&&twice.selected.size()==once.selected.size());
    for(size_t i=0;i<once.selected.size();++i)CHECK(twice.selected[i].pattern==once.selected[i].pattern&&twice.selected[i].root==once.selected[i].root&&twice.selected[i].covered==once.selected[i].covered);
  }
  limeburg::RuleSet rules{{{1,"reg","const","reg",{},4,"CONST",0},{2,"imm","const","imm",{},0,"",0},{3,"reg","add","reg",{"reg","imm"},1,"ADDI",0},{4,"reg","add","reg",{"reg","reg"},1,"ADD",0}},{{"reg",1},{"imm",2}}};
  rules.rules[1].immediate=std::pair<int64_t,int64_t>{-8,7};
  std::vector<limeburg::Node> tree{{1,"add","i64",{20,10}},{20,"const","i64",{},100,true},{10,"const","i64",{},7,true}};
  auto burs=take(limeburg::select(tree,1,rules,"reg"));CHECK(burs.cost==5&&burs.chosen.at(1).rule==3);
  CHECK(burs.chosen.at(20).rule==1&&burs.chosen.at(10).rule==2);
  auto states=take(limeburg::analyze(tree,1,rules,true));CHECK(states.states.at(1).at(1).rule==3&&states.states.at(1).at(1).cost==5);
  CHECK(states.attempts.size()==tree.size()*rules.rules.size());CHECK(take(limeburg::analyze(tree,1,rules)).attempts.empty());
  auto dump=take(limeburg::print_analysis(states,rules));CHECK(dump.find("reg (#1) cost 5 via rule 3 -> ADDI")!=std::string::npos&&dump.find("outside legal range")!=std::string::npos);
  auto forged_state=states;forged_state.states.at(1).at(1).rule=100;fails(limeburg::print_analysis(forged_state,rules),Error::Code::InvalidArgument);
  auto rejection=std::find_if(states.attempts.begin(),states.attempts.end(),[](auto& attempt){return attempt.node==20&&attempt.rule==2;});CHECK(rejection!=states.attempts.end()&&!rejection->cost&&rejection->reason.find("range")!=std::string::npos);
  auto absent=rules;absent.rules.erase(absent.rules.begin());absent.rules[1].origin="target.rules:8:1";auto no_derivation=limeburg::select(tree,1,absent,"reg");fails(no_derivation,Error::Code::Unsatisfiable);CHECK(no_derivation.error().message.find("available {}")!=std::string::npos&&no_derivation.error().message.find("@target.rules:8:1")!=std::string::npos&&no_derivation.error().message.find("node 20 (const): cannot derive reg")!=std::string::npos);
  auto sir=take(limeburg::emit_scheduler(tree,rules,burs));CHECK(sir.instructions.size()==2&&sir.instructions[0].id==20);
  CHECK(sir.instructions[1].uses==std::vector<uint32_t>{20}&&sir.instructions[1].immediates==std::vector<std::pair<uint32_t,int64_t>>{{10,7}});
  take(schedrow::verify(sir,{},take(schedrow::schedule(sir,{}))));
  auto forged_burs=burs;forged_burs.chosen.at(20).rule=2;fails(limeburg::emit_scheduler(tree,rules,forged_burs),Error::Code::Conflict);
  auto tied_rules=rules;tied_rules.rules.push_back({5,"reg","add","reg",{"reg","imm"},1,"ADDI_SECOND",0});std::reverse(tied_rules.rules.begin(),tied_rules.rules.end());
  CHECK(take(limeburg::select(tree,1,tied_rules,"reg")).chosen.at(1).rule==3);
  auto stable=take(limeburg::analyze(tree,1,tied_rules,true));std::reverse(tied_rules.rules.begin(),tied_rules.rules.end());auto stable_copy=take(limeburg::analyze(tree,1,tied_rules,true));CHECK(stable.states==stable_copy.states&&stable.attempts.size()==stable_copy.attempts.size());for(size_t k=0;k<stable.attempts.size();++k)CHECK(stable.attempts[k].node==stable_copy.attempts[k].node&&stable.attempts[k].rule==stable_copy.attempts[k].rule&&stable.attempts[k].reason==stable_copy.attempts[k].reason);
  auto nonvalue=tree;nonvalue[1].produces_value=false;fails(limeburg::select(nonvalue,1,rules,"reg"),Error::Code::InvalidArgument);
  auto dag=tree;dag[0].children={10,10};fails(limeburg::select(dag,1,rules,"reg"),Error::Code::Unsupported);
  auto cyclic=tree;cyclic[1].children={1};fails(limeburg::select(cyclic,1,rules,"reg"),Error::Code::Conflict);
  auto bad_rules=rules;bad_rules.rules[0].cost=-1;fails(limeburg::validate(bad_rules),Error::Code::InvalidArgument);
  fails(limeburg::select(tree,1,rules,"unknown"),Error::Code::InvalidArgument);
  limeburg::RuleSet leaves{{{1,"reg","const","reg",{},1,"CONST"},{2,"imm","const","imm",{},0,""},{3,"reg","add","reg",{"reg","imm"},1,"ADDI"},{4,"reg","add","reg",{"reg","reg"},3,"ADD"}},{{"reg",1},{"imm",2}}};
  auto classified=tree;classified[1].register_class="G";classified[2].register_class="G";auto immediate_selection=take(limeburg::select(classified,1,leaves,"reg"));auto immediate_region=take(limeburg::emit_scheduler(classified,leaves,immediate_selection));CHECK(immediate_region.instructions.back().opcode=="ADDI"&&!immediate_region.instructions.back().register_classes.contains(10)&&immediate_region.instructions.back().register_classes.at(20)=="G");
  classified[2].side_effect=true;leaves.rules[0].supports_side_effects=true;leaves.rules[1].supports_side_effects=true;auto effect_selection=take(limeburg::select(classified,1,leaves,"reg"));auto effect_region=take(limeburg::emit_scheduler(classified,leaves,effect_selection));CHECK(effect_region.instructions.size()==3&&effect_region.instructions.back().opcode=="ADD"&&effect_region.instructions[1].barrier);
  // Deep flat source trees must not recurse during recovery or IR emission.
  limeburg::RuleSet unary{{{1,"reg","const","reg",{},1,"CONST"},{2,"reg","neg","reg",{"reg"},1,"NEG"}},{{"reg",1}}};
  std::vector<limeburg::Node> deep{{0,"const","i64",{},7,true}};
  for(uint32_t id=1;id<20000;++id)deep.push_back({id,"neg","i64",{id-1}});
  auto deep_selection=take(limeburg::select(deep,19999,unary,"reg"));auto deep_region=take(limeburg::emit_scheduler(deep,unary,deep_selection));CHECK(deep_selection.cost==20000&&deep_region.instructions.size()==20000&&deep_region.instructions.front().id==0&&deep_region.instructions.back().uses==std::vector<uint32_t>{19998});
});}
