#include "test.hpp"
#include "limestone.hpp"
#include "limestone.h"

int main(){return test_main([]{
  using namespace limestone;
  auto result=take(run_pipeline("((lambda x (add x 2)) 40)"));
  CHECK(result.machine_ir.find("const.i64 #42")!=std::string::npos&&result.machine_ir.find("ret %1")!=std::string::npos);
  CHECK(result.selected.instructions.size()==2&&result.scheduled.size()==2&&result.stages.back()=="machine-ir");
  CHECK(take(run_pipeline("(add 1 2)",{true,false,false})).scheduled.empty());
  fails(run_pipeline("hello"),Error::Code::InvalidArgument);fails(run_pipeline("(add 1)"),Error::Code::InvalidArgument);
  fails(run_pipeline("3",{true,true,true}),Error::Code::Unsupported);
  unisel::Program graph{{{10,"const",{},1,"i64"},{20,"const",{},2,"i64"},{1,"add",{10,20},{},"i64"}},{1}};
  PipelineTarget target;target.name="fixture";target.patterns={{1,"constant","const","CONST",{},1},{2,"add","add","ADD",{"v","v"},1}};
  target.instructions={{"CONST",{0,1,{},"integer","integer"}},{"ADD",{2,1,{{"ALU",1,1}},"integer","integer"}}};
  target.scheduling={{{"ALU",1}},1};target.register_classes={{"G",{0,1,2}}};target.default_register_class="G";
  auto allocated=take(run_pipeline(graph,target,{true,true,true}));
  CHECK(allocated.allocation&&allocated.allocation->regs.size()==3&&allocated.allocation->spilled.empty());
  take(regtl::verify(allocated.allocation_problem,*allocated.allocation));take(schedrow::verify(allocated.selected,target.scheduling,allocated.scheduled));
  CHECK(allocated.machine_ir.find("ADD %10 %20")!=std::string::npos);
  CHECK(allocated.machine_ir==take(run_pipeline(graph,target,{true,true,true})).machine_ir);
  unisel::Program movable;movable.blocks={{0,"entry",{1}},{1,"exit"}};
  movable.nodes={{2,"branch",{}, {},"",0,true,true,false},{1,"const",{},42,"i64",1},{3,"return",{1},{},"",1,true,true,false}};
  movable.nodes[0].control=schedrow::ControlFlow::Branch;movable.nodes[0].terminator=true;movable.nodes[0].block_targets={1};movable.nodes[2].control=schedrow::ControlFlow::Return;movable.nodes[2].terminator=true;movable.outputs={1};
  auto motion_target=target;motion_target.patterns.push_back({3,"branch","branch","BR",{},1,{},true});motion_target.patterns.push_back({4,"return","return","RET",{"v"},1,{},true});motion_target.instructions["BR"].latency=0;motion_target.instructions["RET"].latency=0;
  PipelineOptions motion_options;motion_options.allocate=true;motion_options.motion={{1,0,2}};
  auto motion_module=take(run_pipeline(movable,motion_target,motion_options));CHECK(motion_module.selected.instructions[0].id==1&&motion_module.selected.instructions[0].block==0);CHECK(std::find(motion_module.stages.begin(),motion_module.stages.end(),"motion")!=motion_module.stages.end());take(schedrow::verify(motion_module.selected,motion_target.scheduling,motion_module.scheduled));
  motion_options.motion_work_limit=0;fails(run_pipeline(movable,motion_target,motion_options),Error::Code::ResourceLimit);
  auto bounded=target;bounded.scheduling.issue_width=3;bounded.instructions["CONST"].latency_range=schedrow::LatencyRange{1,8};bounded.instructions["CONST"].result_latency_ranges={{0,{1,6}}};bounded.instructions["CONST"].operand_latencies={{0,"ADD",0,{0,2},false},{0,"ADD",1,{1,3},false}};auto bounded_module=take(run_pipeline(graph,bounded,{false,true,true}));auto add_issue=std::find_if(bounded_module.scheduled.begin(),bounded_module.scheduled.end(),[](auto& issue){return issue.id==1;});CHECK(add_issue!=bounded_module.scheduled.end()&&add_issue->cycle==3);take(schedrow::verify(bounded_module.selected,bounded.scheduling,bounded_module.scheduled));
  auto timing_description=take(unisel::load_umd(R"(machine bounds {operator const(0);instruction C {scheduling={latency={min=1;max=8;};result_latencies={"0"=[1,6];};operand_latencies=[{result=0;consumer=ADD;use=0;cycles=[0,2];}];};}pattern c: const():i64 -> C;})"));auto timing_target=take(make_target(timing_description.machine));CHECK(timing_target.instructions.at("C").latency_range==schedrow::LatencyRange({1,8})&&timing_target.instructions.at("C").result_latency_ranges.at(0)==schedrow::LatencyRange({1,6}));
  unisel::Program reuse{{{1,"const",{},1,"i64"},{2,"add",{1,1},{},"i64"},{3,"const",{},2,"i64"},{4,"add",{3,3},{},"i64"}}, {4}};
  reuse.dependencies={{2,3,schedrow::DepKind::Ordering}};
  auto scarce=target;scarce.register_classes={{"G",{0}}};scarce.scheduling.issue_width=2;scarce.instructions["CONST"].latency=3;scarce.instructions["ADD"].resources.clear();scarce.instructions["ADD"].latency=0;
  auto reused=take(run_pipeline(reuse,scarce,{false,true,true}));CHECK(reused.allocation->spilled.empty());CHECK(std::any_of(reused.selected.deps.begin(),reused.selected.deps.end(),[](auto& d){return d.kind==schedrow::DepKind::Output&&d.scheduler_only;}));take(schedrow::verify(reused.selected,scarce.scheduling,reused.scheduled));
  auto finalized=take(regtl::allocated_dependencies(reused.selected,*reused.allocation));take(schedrow::verify(finalized,scarce.scheduling,reused.scheduled));
  CHECK(std::find(reused.stages.begin(),reused.stages.end(),"allocated-schedule")!=reused.stages.end());
  unisel::Program stores{{{0,"input",{},{},"i64",0,false},{90,"store",{0},{},"",0,true,true,false},{10,"store",{0},{},"",0,true,true,false}},{}};
  stores.nodes[1].access=schedrow::MemoryAccess{false,true};stores.nodes[2].access=schedrow::MemoryAccess{false,true};
  PipelineTarget memory_target;memory_target.name="memory";memory_target.patterns={{1,"store","store","STORE",{"i64"},1,{},true}};memory_target.instructions={{"STORE",{0}}};
  limeburg::Rule store_rule{1,"value","store","value",{"value"},1,"STORE"};store_rule.supports_side_effects=true;
  memory_target.burs_rules=limeburg::RuleSet{{store_rule},{{"value",0}}};
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){PipelineOptions memory_options;memory_options.selector=selector;auto kept=take(run_pipeline(stores,memory_target,memory_options));CHECK(kept.selected.instructions[0].id==90&&kept.scheduled[0].id==90&&kept.scheduled[1].id==10);}
  bool optimized=false;target.optimizer=[&](const unisel::Program& p){optimized=true;return Result<unisel::Program>::ok(p);};
  take(run_pipeline(graph,target));CHECK(optimized);optimized=false;take(run_pipeline(graph,target,{false,true,false}));CHECK(!optimized);
  auto missing=target;missing.instructions.erase("ADD");fails(run_pipeline(graph,missing),Error::Code::Unsupported);
  fails(run_pipeline(graph,missing,{false,false,false}),Error::Code::Unsupported);
  auto unknown=target;unknown.instructions.at("ADD").latency.reset();fails(run_pipeline(graph,unknown),Error::Code::Unsupported);
  target.optimizer=[](const unisel::Program&){return Result<unisel::Program>::err({Error::Code::Conflict,"adapter rejected input"});};fails(run_pipeline(graph,target),Error::Code::Conflict);
  target.optimizer=[](const unisel::Program&)->Result<unisel::Program>{throw Error{Error::Code::Interrupted,"host cancelled"};};auto cancelled=run_pipeline(graph,target);CHECK(!cancelled&&cancelled.error().code==Error::Code::Interrupted&&cancelled.error().message=="optimize: host cancelled");
  target.optimizer={};target.grouping_adapter=[](const auto&,const auto&)->Result<std::vector<schedrow::InstructionGroup>>{throw std::runtime_error("host grouping failed");};auto failed_group=run_pipeline(graph,target);CHECK(!failed_group&&failed_group.error().code==Error::Code::Internal&&failed_group.error().message=="group: host grouping failed");
  target.grouping_adapter={};target.allocation_adapter=[](const auto&,const auto&,auto)->Result<regtl::Program>{throw std::bad_alloc();};fails(run_pipeline(graph,target,{false,true,true}),Error::Code::ResourceLimit);
  target.allocation_adapter={};target.backend=[](const Module&)->Result<BackendOutput>{throw Error{Error::Code::Unsupported,"backend unavailable"};};PipelineOptions encode;encode.encode=true;auto failed_backend=run_pipeline(graph,target,encode);CHECK(!failed_backend&&failed_backend.error().code==Error::Code::Unsupported&&failed_backend.error().message=="encode: backend unavailable");
  limestone_error error{};auto* module=limestone_compile_checked("(mul 6 7)",nullptr,&error);CHECK(module&&error.code==LIMESTONE_OK);
  CHECK(std::string(limestone_module_text(module)).find("#42")!=std::string::npos);limestone_module_destroy(module);
  CHECK(!limestone_compile_checked(nullptr,nullptr,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  CHECK(!limestone_compile_checked("(",nullptr,&error)&&error.code==LIMESTONE_PARSE&&error.message[0]);
  limestone_options invalid{2,1,0};CHECK(!limestone_compile_checked("1",&invalid,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
});}
