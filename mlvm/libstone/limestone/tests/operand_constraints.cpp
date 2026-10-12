#include "test.hpp"
#include "limestone/limestone.hpp"
#include "unisel/umd.hpp"
#include "limeburg/text.hpp"
#include "schedrow/memory_metadata.hpp"

using namespace limestone;

static unisel::Document scalar(std::string constraint) {
  return take(unisel::load_umd("machine scalar { operator const(0);operator add(2);"
    "instruction C {latency=0;} instruction A {latency=0;} instruction F {latency=0;}"
    "pattern constant:const():i64 -> C cost 10;pattern ordinary:add(?x:i64,?y:i64):i64 -> A cost 10;"
    "pattern fused:add(?x:i64,const():i64 binding imm):i64 -> F cost 1 where {constraints=["+constraint+"];};}"
    "program graph {node %1=input():i64 required false;node %2=const(8):i64;node %3=add(%1,%2):i64;output %3;}","predicates.umd"));
}
static void cases(const std::string& constraint,const std::vector<int64_t>& valid,const std::vector<int64_t>& invalid) {
  auto document=scalar(constraint);auto canonical=unisel::print_umd(document.machine);
  auto copied=take(unisel::load_umd(canonical));CHECK(unisel::print_umd(copied.machine)==canonical&&copied.machine.patterns[2].constraints==document.machine.patterns[2].constraints);
  auto target=take(make_target(copied.machine));
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}) {
    PipelineOptions options;options.selector=selector;
    for(auto value:valid){auto program=*document.program;program.nodes[1].constant=value;auto output=take(run_pipeline(program,target,options));CHECK(output.selected.instructions.size()==1&&output.selected.instructions[0].opcode=="F"&&output.selected.instructions[0].uses==std::vector<uint32_t>{1}&&output.selected.instructions[0].immediates[0].second==value);}
    for(auto value:invalid){auto program=*document.program;program.nodes[1].constant=value;auto output=take(run_pipeline(program,target,options));CHECK(output.selected.instructions.size()==2&&output.selected.instructions.back().opcode=="A");}
  }
  auto only=target;only.patterns.erase(only.patterns.begin()+1);only.burs_rules->rules.erase(only.burs_rules->rules.begin()+1);
  auto program=*document.program;program.nodes[1].constant=invalid.front();
  fails(run_pipeline(program,only),Error::Code::Unsatisfiable);PipelineOptions options;options.selector=SelectionStrategy::BURS;auto failed=run_pipeline(program,only,options);fails(failed,Error::Code::Unsatisfiable);CHECK(failed.error().message.find("operand predicate")!=std::string::npos);
}
int main(){return test_main([] {
  cases("{kind=power_of_two;operand=imm;}",{1,8,int64_t{1}<<62},{0,-8,7,INT64_MIN});
  cases("{kind=multiple_of;operand=imm;value=4;}",{0,8,-8,INT64_MIN},{1,-1,6,INT64_MAX});
  cases("{kind=signed_bits;operand=imm;value=8;}",{-128,0,127},{-129,128,INT64_MIN,INT64_MAX});
  cases("{kind=unsigned_bits;operand=imm;value=8;}",{0,255},{-1,256,INT64_MIN});
  cases("{kind=immediate_eq;operand=imm;value=8;}",{8},{0,9});
  cases("{kind=immediate_ne;operand=imm;value=8;}",{0,9},{8});
  cases("{kind=immediate_lt;operand=imm;value=0;}",{-1,INT64_MIN},{0,1,INT64_MAX});
  cases("{kind=immediate_le;operand=imm;value=0;}",{-1,0,INT64_MIN},{1,INT64_MAX});
  cases("{kind=immediate_gt;operand=imm;value=0;}",{1,INT64_MAX},{0,-1,INT64_MIN});
  cases("{kind=immediate_ge;operand=imm;value=0;}",{0,1,INT64_MAX},{-1,INT64_MIN});
  auto document=scalar("{kind=signed_bits;operand=imm;value=64;},{kind=multiple_of;operand=imm;value=1;}");
  for(auto n:{INT64_MIN,INT64_MAX}){document.program->nodes[1].constant=n;CHECK(take(unisel::solve(*document.program,document.machine.patterns)).cost==1);}
  auto metadata=metacode::operand_constraints_metadata(document.machine.patterns[2].constraints);CHECK(take(metacode::load_operand_constraints(metadata))==document.machine.patterns[2].constraints);

  for(auto kind:{"same_value","different_value"}) {
    auto machine=take(unisel::load_umd(std::string("machine pair {operator pair(2);instruction P {latency=0;}pattern pair:pair(?x:i64,?y:i64):i64 -> P where {constraints=[{kind=")+kind+";operand=x;other=y;}];};}"));
    auto target=take(make_target(machine.machine));
    unisel::Program graph{{{1,"input",{}, {},"i64",0,false},{2,"input",{}, {},"i64",0,false},{3,"pair",{1,2},{},"i64"}},{3}};
    for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}) {
      PipelineOptions options;options.selector=selector;bool same=std::string_view(kind)=="same_value";graph.nodes[2].inputs={1,same?1u:2u};CHECK(take(run_pipeline(graph,target,options)).selected.instructions[0].uses==graph.nodes[2].inputs);
      graph.nodes[2].inputs={1,same?2u:1u};fails(run_pipeline(graph,target,options),Error::Code::Unsatisfiable);
    }
  }
  // Shared constants carry proven values through BURS forest boundaries, but
  // the resulting selected register operand must not turn into an immediate.
  document=scalar("{kind=power_of_two;operand=imm;}");document.machine.patterns[2].tree->inputs[1]=unisel::PatternTree{"","imm","i64"};document.program->outputs.push_back(2);
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}) {
    PipelineOptions options;options.selector=selector;auto module=take(run_pipeline(*document.program,take(make_target(document.machine)),options));CHECK(module.selected.instructions.size()==2&&module.selected.instructions.back().opcode=="F"&&module.selected.instructions.back().uses==std::vector<uint32_t>({1,2})&&module.selected.instructions.back().immediates.empty());
  }
  document.program->nodes[1].constant.reset();document.program->nodes[1].op="input";document.program->nodes[1].required=false;CHECK(take(unisel::solve(*document.program,document.machine.patterns)).selected[0].pattern!=document.machine.patterns[2].id);

  auto rules=take(limeburg::load_rules(R"(ruleset predicates {nonterminal reg;terminal C(0);terminal A(2);
    rule 1 reg:C():i64 -> C cost 10;
    rule 2 reg:A(reg,reg) -> A cost 10;
    rule 3 reg:A(reg,C():i64 binding imm) -> F cost 1 where {constraints=[{kind=power_of_two;operand=imm;},{kind=unsigned_bits;operand=imm;value=8;}];};
  } tree graph {node %1=C():i64 immediate 99;node %2=C():i64 immediate 8;node %3=A(%1,%2):i64;root %3:reg;})","predicates.limeburg"));
  auto canonical=take(limeburg::print_rules(rules));auto copied=take(limeburg::load_rules(canonical));CHECK(take(limeburg::print_rules(copied))==canonical);
  auto& tree=copied.trees[0];CHECK(take(limeburg::select(tree.nodes,tree.root,copied.rules,"reg")).cost==11);tree.nodes[1].imm=7;
  CHECK(take(limeburg::select(tree.nodes,tree.root,copied.rules,"reg")).cost==30);
  auto trace=take(limeburg::analyze(tree.nodes,tree.root,copied.rules,true));CHECK(take(limeburg::print_analysis(trace,copied.rules)).find("power_of_two failed for binding imm")!=std::string::npos);
  auto known=take(limeburg::load_rules(R"(ruleset x {nonterminal reg;terminal I(0);terminal A(1);
    rule reg:I() -> "" cost 0 external_only true;
    rule reg:A(reg binding x) -> P where {constraints=[{kind=power_of_two;operand=x;}];};
  }tree t {node %1=I() required false known_constant 8;node %2=A(%1);root %2:reg;})"));
  auto known_text=take(limeburg::print_rules(known));CHECK(take(limeburg::print_rules(take(limeburg::load_rules(known_text))))==known_text);
  auto selected=take(limeburg::select(known.trees[0].nodes,2,known.rules,"reg"));auto emitted=take(limeburg::emit_scheduler(known.trees[0].nodes,known.rules,selected));CHECK(emitted.instructions[0].uses==std::vector<uint32_t>{1}&&emitted.instructions[0].immediates.empty());

  for(auto constraint:{"{kind=multiple_of;operand=imm;value=0;}","{kind=multiple_of;operand=imm;value=-1;}","{kind=signed_bits;operand=imm;value=65;}","{kind=unsigned_bits;operand=imm;value=0;}","{kind=power_of_two;operand=imm;value=8;}","{kind=same_value;operand=imm;}","{kind=immediate_eq;operand=imm;value=18446744073709551615;}"}) {
    auto invalid=std::string("machine x {operator C(0);instruction I {} pattern p:C() binding imm -> I where {constraints=[")+constraint+"];};}";fails(unisel::load_umd(invalid),Error::Code::InvalidArgument);
  }
  fails(unisel::load_umd("machine x {operator C(0);instruction I {} pattern p:C() -> I where {constraints=[{kind=power_of_two;operand=missing;}];};}"),Error::Code::InvalidArgument);
  fails(unisel::load_umd("machine x {operator C(0);instruction I {} pattern p:C() -> I where {constraints=[{kind=unproved;operand=x;}];};}"),Error::Code::Unsupported);
  fails(limeburg::load_rules("nonterminal r;terminal C(0);rule r:C() -> I where {constraints=[{kind=power_of_two;operand=missing;}];};"),Error::Code::InvalidArgument);
  fails(limeburg::load_rules("nonterminal r;terminal C(0);rule r:C() -> I where {constraints=[{kind=unproved;operand=x;}];};"),Error::Code::Unsupported);
  auto from_isa=take(unisel::from_metacode(take(metacode::parse_isa(R"(arch predicates {} op C {tooling={instruction_selection={selection_tree="const():i64 binding imm";where={constraints=[{kind=multiple_of;operand=imm;value=4;}];};};};})"))));
  CHECK(from_isa.patterns[0].constraints.size()==1);unisel::Program program{{{1,"const",{},8,"i64"}},{1}};CHECK(take(unisel::solve(program,from_isa.patterns)).cost==1);program.nodes[0].constant=7;fails(unisel::solve(program,from_isa.patterns),Error::Code::Unsatisfiable);
  auto decorated=take(unisel::load_umd(R"(machine m {operator const(0);instruction C {latency=0;}
    pattern p:const():i64 -> C where {predicates=[{name=literal;parameters={label="symbol";};}];};}
    program p {node %7=const("symbol",42):i64 properties {metadata={mode=exact;};};output %7;})"));
  auto host_text=unisel::print_umd(decorated.machine);CHECK(unisel::print_umd(take(unisel::load_umd(host_text)).machine)==host_text);
  fails(unisel::solve(*decorated.program,decorated.machine.patterns),Error::Code::Unsupported);
  auto proof=[](const metacode::Value::Object& context,const metacode::Value::Object& parameters)->Result<bool>{auto& root=std::get<metacode::Value::Object>(context.at("root").data);auto metadata=take(metacode::load_operand_metadata(root.at("metadata")));return Result<bool>::ok(root.at("immediate").text()=="42"&&metadata.strings.size()==1&&metadata.strings[0].index==0&&metadata.strings[0].value==parameters.at("label").text()&&metadata.properties.at("mode").text()=="exact");};
  decorated.machine.patterns[0].host_constraints[0].prove=proof;
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){PipelineOptions options;options.selector=selector;auto target=take(make_target(decorated.machine));auto result=take(run_pipeline(*decorated.program,target,options));CHECK(result.selected.instructions[0].source_metadata.at(7).strings[0].value=="symbol");decorated.program->nodes[0].metadata.strings[0].value="unknown";fails(run_pipeline(*decorated.program,target,options),Error::Code::Unsatisfiable);decorated.program->nodes[0].metadata.strings[0].value="symbol";}
  decorated.machine.patterns[0].host_constraints.clear();fails(unisel::solve(*decorated.program,decorated.machine.patterns),Error::Code::Unsatisfiable);
  decorated.program->nodes[0].metadata.strings[0].index=2;fails(unisel::validate(*decorated.program,decorated.machine.patterns),Error::Code::InvalidArgument);

  auto memory=take(unisel::load_umd(R"(machine m {operator load(1);instruction L {latency=0;}instruction LL {latency=0;}
    pattern l:load(?address:i64):i64 -> L cost 2 side_effects true;
    pattern ll:load(load(?address:i64):i64):i64 -> LL cost 1 side_effects true
      where {predicates=[{name=chain;}];} memory_contract {read=true;address_space=heap;size=8;alignment=8;};}
    program p {node %1=input():i64 required false;node %2=load(%1):i64 properties {access={read=true;address_space=heap;alias_sets=[1];size=8;alignment=8;};};
      node %3=load(%2):i64 properties {access={read=true;address_space=heap;alias_sets=[2];size=8;alignment=8;};};output %3;})"));
  auto fusion_text=unisel::print_umd(memory.machine);CHECK(unisel::print_umd(take(unisel::load_umd(fusion_text)).machine)==fusion_text);
  auto chain_proof=[](const metacode::Value::Object& context,const auto&)->Result<bool>{auto& order=std::get<metacode::Value::Array>(context.at("source_order").data);auto summary=take(schedrow::metadata::load_memory_access(context.at("memory_contract")));bool proved=order.size()==2&&summary.read&&!summary.write&&summary.alias_sets.empty();if(order.size()==2){proved&=std::get<metacode::Value::Object>(order[0].data).at("id").text()=="2"&&std::get<metacode::Value::Object>(order[1].data).at("id").text()=="3";}for(auto& value:order){auto& node=std::get<metacode::Value::Object>(value.data);auto& access=std::get<metacode::Value::Object>(node.at("memory").data);proved&=access.at("address_space").text()=="heap"&&!std::get<bool>(access.at("atomic").data)&&access.at("size").text()=="8";}return Result<bool>::ok(proved);};
  memory.machine.patterns[1].host_constraints[0].prove=chain_proof;
  auto invalid_memory=memory.machine.patterns;invalid_memory[1].fused_memory->ordering=schedrow::MemoryOrdering(99);fails(unisel::validate(*memory.program,invalid_memory),Error::Code::InvalidArgument);
  auto invalid_program=*memory.program;invalid_program.nodes[1].access->address_space=std::string("heap\0bad",8);fails(unisel::validate(invalid_program,memory.machine.patterns),Error::Code::InvalidArgument);
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){PipelineOptions options;options.selector=selector;auto target=take(make_target(memory.machine));auto result=take(run_pipeline(*memory.program,target,options));CHECK(result.selected.instructions.size()==1&&result.selected.instructions[0].opcode=="LL"&&result.selected.instructions[0].access->alias_sets.empty());memory.program->nodes[1].access->atomic=true;result=take(run_pipeline(*memory.program,target,options));CHECK(result.selected.instructions.size()==2);memory.program->nodes[1].access->atomic=false;}
  memory.machine.patterns[1].fused_memory.reset();for(auto selector:{SelectionStrategy::Global,SelectionStrategy::BURS}){PipelineOptions options;options.selector=selector;CHECK(take(run_pipeline(*memory.program,take(make_target(memory.machine)),options)).selected.instructions.size()==2);}
  auto rich_rules=take(limeburg::load_rules(R"(ruleset m {nonterminal r;terminal I(0);terminal L(1);
    rule r:I() -> "" external_only true cost 0;
    rule r:L(L(r binding address)) -> LL side_effects true where {predicates=[{name=chain;}];}
      memory_contract {read=true;address_space=heap;size=8;alignment=8;};}
    tree t {node %1=I() required false;node %2=L(%1) properties {access={read=true;address_space=heap;size=8;alignment=8;};};
      node %3=L(%2) properties {access={read=true;address_space=heap;size=8;alignment=8;};strings=[{index=1;value="symbol";}];metadata={mode=exact;};};root %3:r;})"));
  auto rich_text=take(limeburg::print_rules(rich_rules));CHECK(take(limeburg::print_rules(take(limeburg::load_rules(rich_text))))==rich_text);rich_rules.rules.rules[1].host_constraints[0].prove=chain_proof;
  auto rich_selection=take(limeburg::select(rich_rules.trees[0].nodes,3,rich_rules.rules,"r"));auto rich_region=take(limeburg::emit_scheduler(rich_rules.trees[0].nodes,rich_rules.rules,rich_selection));CHECK(rich_region.instructions.size()==1&&rich_region.instructions[0].source_metadata.at(3).strings[0].index==1&&rich_region.instructions[0].memory);
  auto invalid_rules=rich_rules.rules;invalid_rules.rules[1].fused_memory->ordering=schedrow::MemoryOrdering(99);fails(limeburg::validate(invalid_rules),Error::Code::InvalidArgument);
  rich_rules.trees[0].nodes[1].access->atomic=true;fails(limeburg::select(rich_rules.trees[0].nodes,3,rich_rules.rules,"r"),Error::Code::Unsatisfiable);
  fails(limeburg::load_rules("nonterminal r;terminal L(0);rule r:L() -> L memory_contract {read=true;};"),Error::Code::InvalidArgument);
  fails(limeburg::load_rules("nonterminal r;terminal L(0);rule r:L() -> L;tree t {node %1=L() properties {access={alignment=3;};};root %1:r;}"),Error::Code::InvalidArgument);
  fails(limeburg::load_rules("nonterminal r;terminal L(0);rule r:L() -> L;tree t {node %1=L() properties {strings=[{index=1;value=x;}];};root %1:r;}"),Error::Code::InvalidArgument);
});}
