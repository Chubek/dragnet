#include "test.hpp"
#include "limestone.hpp"
#include "limestone.h"
#include "unisel/umd.hpp"
#include "limeburg/target.hpp"
#include "tunah/unisel_adapter.hpp"
#include "bin2bin/bin2bin.hpp"
#include <set>

static const char* description=R"(
machine fixture {
  regclass G = [$r0, $r1, $r2];
  operator const(0); operator add(2);
  instruction CONST { latency=0; }
  instruction ADD { latency=2; resources=[{resource=ALU; duration=1;}]; }
  instruction ADDI { latency=1; resources=[{resource=ALU;}]; }
  pattern 1 constant: const():i64 -> CONST cost 2;
  pattern 2 add: add(?x:i64, ?y:i64):i64 -> ADD cost 2;
  pattern 3 immediate: add(?x:i64, const():i64[-8..7]):i64 -> ADDI cost 1;
  scheduling={issue_width=1; resources={ALU=1;};};
  default_register_class=G;
  custom={retained=[1,"two",true];};
}
program demo {
  node %10=const(100):i64;
  node %20=const(7):i64;
  node %1=add(%10,%20):i64;
  output %1;
}
)";

int main(int argc,char** argv){return test_main([&]{
  using namespace limestone;
  CHECK(argc==2);auto encoded_target=take(make_target(take(metacode::load_isa_file(std::string(argv[1])+"/pipeline-machine.isa"))));
  unisel::Program arithmetic{{{10,"const",{},20,"i64"},{20,"const",{},22,"i64"},{1,"add",{10,20},{},"i64"}},{1}};
  PipelineOptions emit_code;emit_code.allocate=true;emit_code.encode=true;auto binary_module=take(run_pipeline(arithmetic,encoded_target,emit_code));CHECK(binary_module.encoded&&binary_module.encoded->bytes.size()==5&&binary_module.stages.back()=="encode");
  auto decoder=take(bin2bin::from_metacode(take(metacode::load_isa_file(std::string(argv[1])+"/pipeline-machine.isa"))));
  auto instructions=take(bin2bin::decode(decoder,binary_module.encoded->bytes));std::map<std::string,int64_t> machine_state;
  for(auto& i:instructions){if(i.mnemonic=="CONST")machine_state[i.operands.at("dst")]=std::stoll(i.operands.at("imm"));else machine_state[i.operands.at("dst")]=machine_state.at(i.operands.at("lhs"))+machine_state.at(i.operands.at("rhs"));}
  CHECK(machine_state.at(decoder.registers.at("G").at(binary_module.allocation->regs.at(1)))==42);
  emit_code.allocate=false;fails(run_pipeline(arithmetic,encoded_target,emit_code),Error::Code::Unsupported);
  PipelineOptions tracing;tracing.trace_execution=true;auto traced=take(run_pipeline("(if (lt 1 2) (add 20 22) 0)",tracing));CHECK(traced.execution&&traced.execution->value==42);CHECK(traced.machine_ir.find("guard_nonzero")!=std::string::npos&&traced.machine_ir.find("checked.add.i64")!=std::string::npos);
  auto document=take(unisel::load_umd(description,"fixture.umd"));CHECK(document.program&&document.machine.patterns.size()==3);
  auto serialized=unisel::print_umd(document.machine);auto roundtrip=take(unisel::load_umd(serialized));
  CHECK(serialized==unisel::print_umd(roundtrip.machine));CHECK(roundtrip.machine.metadata.at("custom").text()==document.machine.metadata.at("custom").text());
  auto wide=take(unisel::load_umd("machine wide { bits=18446744073709551615; }"));CHECK(std::get<uint64_t>(wide.machine.metadata.at("bits").data)==UINT64_MAX);CHECK(take(unisel::load_umd(unisel::print_umd(wide.machine))).machine.metadata.at("bits").text()=="18446744073709551615");
  auto escaped=take(unisel::load_umd(R"(machine escaped {operator A(0);instruction I {} pattern p:A()->I origin "path\u0000\b\f\uD83D\uDE00"; custom="keep\u0001";})"));auto escaped_copy=take(unisel::load_umd(unisel::print_umd(escaped.machine)));CHECK(escaped.machine.patterns[0].origin==escaped_copy.machine.patterns[0].origin&&escaped.machine.metadata.at("custom").text()==escaped_copy.machine.metadata.at("custom").text());
  fails(unisel::load_umd("machine x {operator A(1);instruction I {} pattern p:A(?x register_class missing)->I;}"),Error::Code::InvalidArgument);
  auto target=take(make_target(document.machine));
  CHECK(target.metadata.at("custom").text()==document.machine.metadata.at("custom").text());
  auto policy=take(unisel::load_umd(R"(machine policy {
    operator left(0); operator right(0);
    instruction LOW {scheduling={latency=0;priority=-3;pressure_delta={G=1;};};custom={retained=true;};}
    instruction HIGH {speculative=false;scheduling={latency=0;priority=7;pressure_delta={G=-1;};};}
    pattern low:left():i64 -> LOW; pattern high:right():i64 -> HIGH;
    scheduling={issue_width=1;};
  } program graph {node %1=left():i64;node %2=right():i64;output %1;output %2;})","policy.umd"));
  auto policy_target=take(make_target(policy.machine));CHECK(policy_target.instructions.at("LOW").metadata.at("custom").text().find("true")!=std::string::npos);
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}) {
    PipelineOptions options;options.selector=selector;auto module=take(run_pipeline(*policy.program,policy_target,options));
    CHECK(module.scheduled[0].id==2&&module.scheduled[0].cycle==0&&module.scheduled[1].cycle==1);
    CHECK(module.selected.instructions[0].priority==-3&&module.selected.instructions[0].pressure_delta.at("G")==1);
    CHECK(!module.selected.instructions[1].speculative&&module.selected.instructions[1].pressure_delta.at("G")==-1);
    auto trapping=*policy.program;trapping.nodes[0].may_trap=true;auto guarded=policy_target;guarded.patterns[0].supports_side_effects=true;guarded.burs_rules->rules[0].supports_side_effects=true;
    module=take(run_pipeline(trapping,guarded,options));CHECK(module.scheduled[0].id==1&&module.scheduled[1].id==2);
    auto hazards=take(schedrow::dependencies(module.selected));CHECK(std::any_of(hazards.deps.begin(),hazards.deps.end(),[](auto& edge){return edge.producer==1&&edge.consumer==2&&edge.kind==schedrow::DepKind::Ordering&&!edge.scheduler_only;}));
  }
  auto bad_policy=policy.machine;std::get<metacode::Value::Object>(bad_policy.metadata.at("scheduling").data)["unsupported_contract"]=metacode::Value(true);
  auto rejected_policy=make_target(bad_policy);fails(rejected_policy,Error::Code::Unsupported);CHECK(rejected_policy.error().message.find("policy.umd:")!=std::string::npos&&rejected_policy.error().message.find("unsupported_contract")!=std::string::npos);
  bad_policy=policy.machine;std::get<metacode::Value::Object>(bad_policy.instructions.at("HIGH").at("scheduling").data)["priority"]=metacode::Value(uint64_t(UINT64_MAX));fails(make_target(bad_policy),Error::Code::InvalidArgument);
  bad_policy=policy.machine;std::get<metacode::Value::Object>(bad_policy.instructions.at("HIGH").at("scheduling").data)["delay_slots"]=metacode::Value(int64_t(2));rejected_policy=make_target(bad_policy);fails(rejected_policy,Error::Code::Unsupported);CHECK(rejected_policy.error().message.find("instruction HIGH")!=std::string::npos);
  bad_policy=policy.machine;bad_policy.metadata["default_register_class"]=metacode::Value(std::string("missing"));fails(make_target(bad_policy),Error::Code::InvalidArgument);
  auto flat=document.machine;flat.patterns={{1,"constant","const","CONST",{},1},{2,"sum","add","ADD",{"i64","i64"},1}};
  auto flat_text=unisel::print_umd(flat);auto flat_copy=take(unisel::load_umd(flat_text));CHECK(flat_copy.machine.patterns[1].tree->inputs[0].type=="i64");
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}) {
    PipelineOptions options;options.selector=selector;
    CHECK(take(run_pipeline(arithmetic,take(make_target(flat)),options)).selected.instructions.size()==3);
    auto mistyped=arithmetic;mistyped.nodes[1].type="f64";fails(run_pipeline(mistyped,take(make_target(flat)),options),Error::Code::Unsatisfiable);
    fails(run_pipeline(mistyped,take(make_target(flat_copy.machine)),options),Error::Code::Unsatisfiable);
  }
  auto anonymous=flat;anonymous.patterns[1].tree=unisel::PatternTree{"add","","i64",{{"","arg0","i64"},{"","","i64"}}};
  auto anonymous_copy=take(unisel::load_umd(unisel::print_umd(anonymous)));CHECK(anonymous_copy.machine.patterns[1].tree->inputs[0].binding!=anonymous_copy.machine.patterns[1].tree->inputs[1].binding);
  CHECK(take(unisel::solve(arithmetic,anonymous_copy.machine.patterns)).selected.size()==3);
  auto included=take(unisel::load_umd_file(std::string(argv[1])+"/arithmetic-includes.umd"));CHECK(included.program&&included.machine.source.file.ends_with("/includes/scalar-machine.umd"));CHECK(included.program->nodes[0].origin.find("/includes/sum.umd:2:")!=std::string::npos);
  auto included_copy=take(unisel::load_umd(unisel::print_umd(included.machine)));CHECK(included_copy.machine.patterns[0].origin==included.machine.patterns[0].origin);
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){PipelineOptions options;options.selector=selector;auto module=take(run_pipeline(*included.program,take(make_target(included.machine)),options));CHECK(module.selected.instructions[0].origin.find("/includes/scalar-machine.umd:7:3")!=std::string::npos);}
  fails(unisel::load_umd("machine x {operator A(0);instruction I {} pattern p:A()->I cost 1 cost 2;}"),Error::Code::Conflict);
  auto included_module=take(run_pipeline(*included.program,encoded_target,{true,true,true,SelectionStrategy::Global,AllocationStrategy::LinearScan,true}));auto included_stream=take(bin2bin::decode(decoder,included_module.encoded->bytes));machine_state.clear();for(auto& i:included_stream){if(i.mnemonic=="CONST")machine_state[i.operands.at("dst")]=std::stoll(i.operands.at("imm"));else machine_state[i.operands.at("dst")]=machine_state.at(i.operands.at("lhs"))+machine_state.at(i.operands.at("rhs"));}CHECK(machine_state.at(included_stream.back().operands.at("dst"))==42);
  auto cycle=unisel::load_umd_file(std::string(argv[1])+"/includes/cycle.umd");fails(cycle,Error::Code::Conflict);CHECK(cycle.error().message.find("cycle.umd:1:1")!=std::string::npos);
  const std::string virtual_root="include \"machine.umd\"; include \"graph.umd\";";size_t resolved=0;syntax::IncludeOptions include_options;
  include_options.resolver=[&](std::string_view file,std::string_view requested){++resolved;CHECK(file=="/virtual/root.umd");if(requested=="machine.umd")return Result<syntax::ResolvedSource>::ok({"/virtual/machine.umd",std::string(description).substr(0,std::string(description).find("program demo"))});if(requested=="graph.umd")return Result<syntax::ResolvedSource>::ok({"/virtual/graph.umd",std::string(description).substr(std::string(description).find("program demo"))});return Result<syntax::ResolvedSource>::err({Error::Code::NotFound,"missing virtual file"});};
  // The root and both included ASTs are validated together; forward references
  // and original source coordinates survive expansion without textual splicing.
  auto virtual_document=take(unisel::load_umd(virtual_root,"/virtual/root.umd",include_options));CHECK(resolved==2&&virtual_document.program&&virtual_document.machine.source.file=="/virtual/machine.umd");
  include_options.documents=2;resolved=0;fails(unisel::load_umd(virtual_root,"/virtual/root.umd",include_options),Error::Code::ResourceLimit);CHECK(resolved==1);
  include_options.documents=128;include_options.depth=0;resolved=0;fails(unisel::load_umd(virtual_root,"/virtual/root.umd",include_options),Error::Code::ResourceLimit);CHECK(!resolved);
  include_options.depth=32;include_options.bytes=virtual_root.size()+std::string(description).size()-1;fails(unisel::load_umd(virtual_root,"/virtual/root.umd",include_options),Error::Code::ResourceLimit);
  include_options.bytes=16*1024*1024;include_options.resolver=[](auto,auto)->Result<syntax::ResolvedSource>{throw std::runtime_error("virtual resolver failure");};auto resolver_failure=unisel::load_umd(virtual_root,"/virtual/root.umd",include_options);fails(resolver_failure,Error::Code::Internal);CHECK(resolver_failure.error().message.find("/virtual/root.umd:1:1")!=std::string::npos);
  include_options.resolver=[](auto,auto){return Result<syntax::ResolvedSource>::ok({"","machine x {}"});};fails(unisel::load_umd(virtual_root,"/virtual/root.umd",include_options),Error::Code::InvalidArgument);
  include_options.resolver=[](auto,auto){return Result<syntax::ResolvedSource>::ok({std::string("file\0alias",10),"machine x {}"});};fails(unisel::load_umd(virtual_root,"/virtual/root.umd",include_options),Error::Code::InvalidArgument);
  fails(unisel::load_umd("machine x {}",std::string_view("bad\0identity",12)),Error::Code::InvalidArgument);
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS})for(auto allocator:{AllocationStrategy::LinearScan,AllocationStrategy::Greedy,AllocationStrategy::GraphColoring,AllocationStrategy::Constraint,AllocationStrategy::PBQP}) {
    PipelineOptions options;options.selector=selector;options.allocator=allocator;options.allocate=true;
    auto module=take(run_pipeline(*document.program,target,options));CHECK(module.allocation&&module.selected.instructions.size()>=2);
    take(regtl::verify(module.allocation_problem,*module.allocation));take(schedrow::verify(module.selected,target.scheduling,module.scheduled));
    CHECK(module.machine_ir==take(run_pipeline(*document.program,target,options)).machine_ir);
  }
  auto no_instruction=std::string(description);auto pos=no_instruction.find("instruction ADDI");no_instruction.erase(pos,no_instruction.find('\n',pos)-pos);fails(unisel::load_umd(no_instruction),Error::Code::NotFound);
  fails(unisel::load_umd("machine x { operator add(2); instruction I {} pattern p: add(?x) -> I; }"),Error::Code::InvalidArgument);
  fails(unisel::load_umd("machine x {} program y { node %x=add(%missing); }"),Error::Code::InvalidArgument);
  auto imported=take(unisel::from_metacode(take(metacode::parse_isa(R"ISA(
arch example { word_size=32; custom={keep=true;}; }
regclass G { r0(32)=0, }
op plus { semantics="(set rd (add a b))"; tooling={instruction_selection={selection_tree="add(?a:i32,?b:i32):i32"; cost=3;};}; }
)ISA"))));CHECK(imported.patterns.size()==1&&imported.instructions.at("plus").contains("semantics"));take(unisel::load_umd(unisel::print_umd(imported)));
  auto no_patterns=take(unisel::from_metacode(take(metacode::parse_isa("arch metadata {} op unproved { semantics=\"(add a b)\"; }"))));CHECK(no_patterns.patterns.empty());
  PipelineOptions encoding;encoding.encode=true;fails(run_pipeline(*document.program,target,encoding),Error::Code::Unsupported);
  target.backend=[](const Module& module){CHECK(module.stages.back()=="machine-ir");return Result<BackendOutput>::ok({{1,2,3},{{0,"fixture","external",0}}});};
  CHECK(take(run_pipeline(*document.program,target,encoding)).encoded->bytes==std::vector<uint8_t>{1,2,3});
  limestone_error error{};auto* module=limestone_compile_umd(description,nullptr,&error);CHECK(module&&error.code==LIMESTONE_OK&&limestone_module_stage_count(module)==4);CHECK(std::string(limestone_module_stage(module,1))=="select");CHECK(!limestone_module_stage(module,100));limestone_module_destroy(module);

  regtl::Function function;
  function.classes={{"G",{0,1}}};function.values={{1,"G"},{2,"G"},{3,"G"}};
  function.blocks={{0,{{0,{1},{},{},{},{}}},{1}},{1,{{1,{2},{1},{},{},{}}},{2}},{2,{{2,{3},{2},{},{},{}}},{1},{3}}};
  auto liveness=take(regtl::analyze(function));CHECK(liveness.live_in.at(1)==std::vector<uint32_t>{1});CHECK(std::find(liveness.live_out.at(2).begin(),liveness.live_out.at(2).end(),1)!=liveness.live_out.at(2).end());
  for(auto allocator:{regtl::linear_scan,regtl::greedy,regtl::graph_color})take(regtl::verify(liveness.problem,take(allocator(liveness.problem))));
  regtl::Function tied;tied.classes={{"G",{0}}};tied.values={{1,"G",{},false},{2,"G",{},false}};tied.blocks={{0,{{0,{1},{}},{1,{2},{1},{},{},{{2,1}}}},{},{2}}};
  auto ties=take(regtl::analyze(tied));auto assignment=take(regtl::graph_color(ties.problem));CHECK(assignment.regs.at(1)==assignment.regs.at(2));
  auto bad_tie=tied;bad_tie.blocks[0].instructions[1].early_defs={2};fails(regtl::graph_color(take(regtl::analyze(bad_tie)).problem),Error::Code::Unsatisfiable);
  fails(regtl::constraint_allocate(ties.problem,0),Error::Code::ResourceLimit);
  auto bad_clobber=tied;bad_clobber.blocks[0].instructions[1].clobbers={100};fails(regtl::analyze(bad_clobber),Error::Code::InvalidArgument);
  using Location=regtl::Location;Location a{Location::Kind::Register,0},b{Location::Kind::Register,1},scratch{Location::Kind::Spill,0};
  std::vector<regtl::Move> moves{{a,b},{b,a}};auto sequential=take(regtl::resolve_parallel_moves(moves,scratch));std::map<Location,int> state{{a,10},{b,20}};for(auto move:sequential)state[move.destination]=state[move.source];CHECK(state.at(a)==20&&state.at(b)==10);
  fails(regtl::resolve_parallel_moves(moves,a),Error::Code::InvalidArgument);
  regtl::Function physical;physical.classes={{"G",{0,1,2}}};physical.aliases={{0,2}};physical.values={{1,"G",{},false},{2,"G",{},false}};physical.blocks={{0,{{0,{1},{}}},{1}},{1,{{1,{2},{1},{},{},{},{},{0}}},{},{2}}};
  auto live_physical=take(regtl::analyze(physical));CHECK(live_physical.physical_live_in.at(0)==std::vector<uint32_t>{0});auto assigned_physical=take(regtl::graph_color(live_physical.problem));CHECK(assigned_physical.regs.at(1)==1);take(regtl::verify(live_physical.problem,assigned_physical));
  schedrow::Region state_region{"state"};schedrow::Instruction produce{},consume{};produce.id=10;produce.implicit_defs={0};produce.latency=3;consume.id=20;consume.implicit_uses={2};state_region.instructions={produce,consume};schedrow::MachineModel state_machine;state_machine.register_aliases={{0,2}};auto state_schedule=take(schedrow::schedule(state_region,state_machine));CHECK(state_schedule[1].cycle>=3);take(schedrow::verify(state_region,state_machine,state_schedule));
  auto repeated=take(unisel::load_umd("machine repeat {regclass G=[$r0,$r1];operator twice(2);instruction T {latency=0;}pattern p: twice(?x:i64 register_class G,?x:i64 register_class G):i64 -> T;}program p {node %1=input():i64 required false properties {class=G;};node %2=twice(%1,%1):i64;output %2;}"));auto repeat_target=take(make_target(repeated.machine));PipelineOptions repeat_options;repeat_options.selector=SelectionStrategy::BURS;CHECK(take(run_pipeline(*repeated.program,repeat_target,repeat_options)).selected.instructions[0].uses==std::vector<uint32_t>({1,1}));
  take(unisel::load_umd(unisel::print_umd(repeated.machine)));auto mismatch=*repeated.program;mismatch.nodes[0].register_class="other";fails(run_pipeline(mismatch,repeat_target,repeat_options),Error::Code::Unsatisfiable);mismatch=*repeated.program;mismatch.nodes.push_back({3,"input",{}, {},"i64",0,false});mismatch.nodes.back().register_class="G";mismatch.nodes[1].inputs[1]=3;fails(run_pipeline(mismatch,repeat_target,repeat_options),Error::Code::Unsatisfiable);

  schedrow::Region memory{"memory"};schedrow::Instruction store{},load{},unrelated{};
  store.id=1;store.access=schedrow::MemoryAccess{false,true,false,false,schedrow::MemoryOrdering::Relaxed,"heap",{1}};
  load.id=2;load.access=schedrow::MemoryAccess{true,false,false,false,schedrow::MemoryOrdering::Relaxed,"heap",{1}};
  unrelated.id=3;unrelated.access=schedrow::MemoryAccess{true,false,false,false,schedrow::MemoryOrdering::Relaxed,"heap",{2}};memory.instructions={store,load,unrelated};
  CHECK(take(schedrow::dependencies(memory)).deps.size()==1);memory.instructions[2].access->ordering=schedrow::MemoryOrdering::Release;CHECK(take(schedrow::dependencies(memory)).deps.size()==3);
  schedrow::Region slots{"slots"};schedrow::Instruction left{},right{};left.id=1;left.issue_slots={0};right.id=2;right.issue_slots={0};slots.instructions={left,right};schedrow::MachineModel machine{{},2};auto issued=take(schedrow::schedule(slots,machine));CHECK(issued[0].cycle!=issued[1].cycle);take(schedrow::verify(slots,machine,issued));
  schedrow::Region loop{"loop"};left.issue_slots.clear();left.resources={{"ALU",2,1}};loop.instructions={left};loop.deps={{1,1,schedrow::DepKind::True,3,1}};machine={{{"ALU",1}},1};
  fails(schedrow::schedule_modulo(loop,machine,{2,8,1000}),Error::Code::Unsatisfiable);
  auto modulo=take(schedrow::schedule_modulo(loop,machine,{3,8,1000}));take(schedrow::verify_modulo(loop,machine,modulo,3));fails(schedrow::verify_modulo(loop,machine,modulo,2),Error::Code::Conflict);
  fails(schedrow::schedule_modulo(loop,machine,{3,8,0}),Error::Code::ResourceLimit);

  tunah::Session session;take(session.load_rules("(operator add_i64 2) (rule zero (add_i64 ?x 0) ?x)"));
  unisel::Program graph{{{1,"input",{}, {},"i64",0,false},{2,"const",{},0,"i64"},{3,"add",{1,2},{},"i64"}},{3}};
  tunah::GraphAdapterOptions adapter;adapter.operators={{"add",{"add_i64","i64",2,true}}};
  auto optimized=take(tunah::optimize_graph(graph,session,adapter));CHECK(optimized.program.outputs==std::vector<uint32_t>{1}&&optimized.program.nodes.size()==1&&optimized.values.at(3)==1);
  auto reversed=graph;std::reverse(reversed.nodes[2].inputs.begin(),reversed.nodes[2].inputs.end());CHECK(take(tunah::optimize_graph(reversed,session,adapter)).program.outputs==optimized.program.outputs);
  auto pinned=graph;pinned.nodes[2].may_trap=true;CHECK(take(tunah::optimize_graph(pinned,session,adapter)).program.nodes.size()==3);
  // A fused arithmetic instruction retains the covered load's exact effects.
  auto memory_target=take(unisel::load_umd(R"(machine memory {regclass G=[$r0,$r1,$r2];operator input(0);operator load(1);operator const(0);operator add(2);instruction LOAD_ADD {latency=3;may_trap=true;access={read=true;address_space=heap;size=8;alignment=8;};}pattern fused:add(load(?p:i64):i64,const():i64[-8..7]):i64 -> LOAD_ADD side_effects true;default_register_class=G;})"));
  unisel::Program memory_graph{{{1,"input",{}, {},"i64",0,false},{90,"load",{1},{},"i64"},{2,"const",{},7,"i64"},{3,"add",{90,2},{},"i64"}},{3}};memory_graph.nodes[1].access=schedrow::MemoryAccess{true,false,false,false,schedrow::MemoryOrdering::Relaxed,"heap",{17},8,8};memory_graph.nodes[1].may_trap=true;
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){PipelineOptions options;options.selector=selector;options.allocate=true;auto fused=take(run_pipeline(memory_graph,take(make_target(memory_target.machine)),options));CHECK(fused.selected.instructions.size()==1&&fused.selected.instructions[0].opcode=="LOAD_ADD"&&fused.selected.instructions[0].access->alias_sets==std::vector<uint32_t>{17}&&fused.selected.instructions[0].may_trap&&fused.selected.instructions[0].immediates[0].second==7);}
  auto unsupported_load=memory_graph;unsupported_load.nodes[1].access->volatile_access=true;fails(run_pipeline(unsupported_load,take(make_target(memory_target.machine))),Error::Code::Conflict);
  auto wrong_type=graph;wrong_type.nodes[0].type="i32";fails(tunah::optimize_graph(wrong_type,session,adapter),Error::Code::InvalidArgument);
  tunah::Session illegal;take(illegal.load_rules("(operator add_i64 2) (operator bogus 1) (rule bad (add_i64 ?x 0) (bogus ?x))"));adapter.costs.operators={{"add_i64",10},{"bogus",0}};fails(tunah::optimize_graph(graph,illegal,adapter),Error::Code::Conflict);
});}
