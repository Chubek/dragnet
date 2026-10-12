#include "test.hpp"
#include "limestone.hpp"
#include "bin2bin/codegen.hpp"
#include "metacode/machine-ir/bridge.hpp"
#include "tunah/unisel_adapter.hpp"
#include <fstream>
#include <set>

// The fixture's execution model is explicit. This interpreter is an independent
// oracle for encoded CFG branches and private-frame transfers.
static int64_t execute(const limestone::bin2bin::Architecture& target,const std::vector<uint8_t>& bytes,uint64_t address=0,std::map<std::string,int64_t> registers={}) {
  using namespace limestone;auto decoded=take(bin2bin::decode(target,bytes,address));std::map<uint64_t,size_t> positions;for(size_t k=0;k<decoded.size();++k)positions[decoded[k].address]=k;std::map<int64_t,int64_t> memory;size_t pc=0;
  for(size_t budget=0;budget<1000&&pc<decoded.size();++budget){auto& i=decoded[pc++];auto& o=i.operands;
    if(i.mnemonic=="CONST")registers[o.at("dst")]=std::stoll(o.at("imm"));
    else if(i.mnemonic=="ADD")registers[o.at("dst")]=registers.at(o.at("lhs"))+registers.at(o.at("rhs"));
    else if(i.mnemonic=="SPILL")memory[std::stoll(o.at("slot"))]=registers.at(o.at("src"));
    else if(i.mnemonic=="RELOAD")registers[o.at("dst")]=memory.at(std::stoll(o.at("slot")));
    else if(i.mnemonic=="JUMP"||(i.mnemonic=="BRANCH"&&registers.at(o.at("cond"))))pc=positions.at(std::stoull(o.at("target")));
    else if(i.mnemonic=="RETURN")return registers.at(o.at("src"));
    else CHECK(i.mnemonic=="BRANCH");
  }throw std::runtime_error("fixture execution did not return");
}

int main(int argc,char** argv){return test_main([&]{
  using namespace limestone;CHECK(argc>=2&&argc<=4);auto metadata=take(metacode::load_isa_file(std::string(argv[1])+"/backend-machine.isa"));auto target=take(make_target(metadata));auto decoder=take(bin2bin::from_metacode(metadata));
  unisel::Program cfg{{{90,"const",{},1,"i64",7},{2,"branch",{90},{},"",7,true,false,false},{40,"const",{},0,"i64",9},{4,"return",{40},{},"",9,true,false,false},{60,"const",{},42,"i64",3},{6,"return",{60},{},"",3,true,false,false}}, {}, {},{{7,"entry",{3,9}},{9,"zero"},{3,"answer"}},7};
  cfg.nodes[1].control=schedrow::ControlFlow::ConditionalBranch;cfg.nodes[1].block_targets={3,9};cfg.nodes[3].control=cfg.nodes[5].control=schedrow::ControlFlow::Return;
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS})for(auto allocator:{AllocationStrategy::LinearScan,AllocationStrategy::Greedy,AllocationStrategy::GraphColoring,AllocationStrategy::Constraint}){
    PipelineOptions options;options.allocate=true;options.encode=true;options.selector=selector;options.allocator=allocator;auto module=take(run_pipeline(cfg,target,options));CHECK(execute(decoder,module.encoded->bytes)==42);auto zero=cfg;zero.nodes[0].constant=0;CHECK(execute(decoder,take(run_pipeline(zero,target,options)).encoded->bytes)==0);
    auto exchange=take(machineir_bridge::deserialize(module.machine_ir_exchange));CHECK(exchange.region.blocks.size()==3&&exchange.region.entry==7);CHECK(take(machineir_bridge::serialize(exchange))==module.machine_ir_exchange);take(schedrow::verify(module.selected,target.scheduling,module.scheduled));take(regtl::verify(module.allocation_problem,*module.allocation));
  }
  auto bad=cfg;bad.nodes[5].inputs={40};fails(run_pipeline(bad,target),Error::Code::Conflict);bad=cfg;bad.nodes[1].block_targets={9,3};PipelineOptions encoded;encoded.allocate=true;encoded.encode=true;fails(run_pipeline(bad,target,encoded),Error::Code::Conflict);bad=cfg;bad.blocks[0].successors={9};fails(run_pipeline(bad,target),Error::Code::Conflict);
  auto adds_effects=target;adds_effects.instructions["CONST"].memory=true;fails(run_pipeline(cfg,adds_effects),Error::Code::Conflict);
  adds_effects=target;adds_effects.instructions["CONST"].may_trap=true;fails(run_pipeline(cfg,adds_effects),Error::Code::Conflict);
  adds_effects=target;adds_effects.instructions["CONST"].control=schedrow::ControlFlow::Return;fails(run_pipeline(cfg,adds_effects),Error::Code::Conflict);
  auto binding=take(bin2bin::encoding_bindings(metadata));std::map<uint32_t,std::string> physical_names{{0,"r0"},{1,"r1"},{2,"r2"},{3,"r3"}};
  auto module=take(run_pipeline(cfg,target,encoded));auto wrong_codec=decoder;
  auto grouped_target=target;grouped_target.grouping_adapter=[](const unisel::Program&,const schedrow::Region& region){std::vector<schedrow::InstructionGroup> groups;for(auto& block:region.blocks){schedrow::InstructionGroup group{block.id,schedrow::GroupKind::Adjacent,{},block.name};for(auto& i:region.instructions)if(i.block==block.id)group.members.push_back(i.id);if(!group.members.empty())groups.push_back(std::move(group));}return Result<std::vector<schedrow::InstructionGroup>>::ok(std::move(groups));};
  auto grouped_module=take(run_pipeline(cfg,grouped_target,encoded));CHECK(execute(decoder,grouped_module.encoded->bytes)==42&&grouped_module.selected.groups.size()==3);CHECK(std::find(grouped_module.stages.begin(),grouped_module.stages.end(),"group")!=grouped_module.stages.end());auto group_exchange=take(machineir_bridge::deserialize(grouped_module.machine_ir_exchange));CHECK(group_exchange.region.groups.size()==3&&take(machineir_bridge::serialize(group_exchange))==grouped_module.machine_ir_exchange);
  for(auto& form:wrong_codec.forms)if(form.mnemonic=="RETURN")form.control=bin2bin::ControlFlow::Fallthrough;
  fails(bin2bin::encode_region(wrong_codec,binding,physical_names,module.selected,module.order,module.allocation),Error::Code::Conflict);
  auto wrong_binding=binding;wrong_binding["BRANCH"]["target"].index=1;
  fails(bin2bin::encode_region(decoder,wrong_binding,physical_names,module.selected,module.order,module.allocation),Error::Code::Conflict);
  wrong_binding=binding;wrong_binding["CONST"].erase("imm");fails(bin2bin::encode_region(decoder,wrong_binding,physical_names,module.selected,module.order,module.allocation),Error::Code::Unsupported);
  auto omitted=module.selected;omitted.instructions[0].uses.push_back(omitted.instructions[0].defs[0]);fails(bin2bin::encode_region(decoder,binding,physical_names,omitted,module.order,module.allocation),Error::Code::Unsupported);
  auto byte_metadata=take(metacode::load_isa_file(std::string(argv[1])+"/control-byte.isa"));auto byte_target=take(make_target(byte_metadata));auto byte_codec=take(bin2bin::from_metacode(byte_metadata));
  unisel::Program trap{{{0,"trap",{}, {},"",0,true,false,false}}, {}};trap.nodes[0].control=schedrow::ControlFlow::Trap;trap.nodes[0].may_trap=true;PipelineOptions terminal_options;terminal_options.encode=true;
  for(auto strategy:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){terminal_options.selector=strategy;auto halt=take(run_pipeline(trap,byte_target,terminal_options));CHECK(halt.encoded->bytes==std::vector<uint8_t>{3}&&take(bin2bin::analyze(byte_codec,halt.encoded->bytes)).blocks[0].successors.empty());}
  auto returning=trap;returning.nodes[0].op="return";returning.nodes[0].control=schedrow::ControlFlow::Return;returning.nodes[0].may_trap=false;auto returned=take(run_pipeline(returning,byte_target,terminal_options));CHECK(returned.encoded->bytes==std::vector<uint8_t>{2});auto forged_byte=byte_codec;forged_byte.control.erase(2);fails(bin2bin::encode_region(forged_byte,{}, {},returned.selected,returned.order,{}),Error::Code::Conflict);
  auto wrong_order=module.order;std::swap(wrong_order[0],wrong_order[1]);
  fails(bin2bin::encode_region(decoder,binding,physical_names,module.selected,wrong_order,module.allocation),Error::Code::Conflict);
  // The second branch promotion pushes the first branch beyond its short range.
  auto relaxed=decoder;auto short_jump=*std::find_if(decoder.forms.begin(),decoder.forms.end(),[](auto& f){return f.mnemonic=="JUMP";});short_jump.id=100;short_jump.width=8;short_jump.mask=15;short_jump.base=12;short_jump.fields[0].lsb=4;short_jump.fields[0].width=4;relaxed.forms.push_back(short_jump);take(bin2bin::validate(relaxed));
  unisel::Program cascade;cascade.blocks={{0,"entry",{3}},{1,"distant",{4}},{2,"padding",{3}},{3,"answer"},{4,"other"}};
  cascade.nodes={{0,"jump",{}, {},"",0,true,false,false},{1,"jump",{}, {},"",1,true,false,false},{2,"const",{},1,"i64",2},{3,"const",{},2,"i64",2},{4,"const",{},3,"i64",2},{5,"const",{},42,"i64",3},{6,"return",{5},{},"",3,true,false,false},{7,"const",{},0,"i64",4},{8,"return",{7},{},"",4,true,false,false}};
  cascade.nodes[0].control=cascade.nodes[1].control=schedrow::ControlFlow::Branch;cascade.nodes[0].block_targets={3};cascade.nodes[1].block_targets={4};cascade.nodes[6].control=cascade.nodes[8].control=schedrow::ControlFlow::Return;
  auto no_spill=target;no_spill.spill_classes.clear();auto cascading=take(run_pipeline(cascade,no_spill,encoded));
  auto cascade_bytes=take(bin2bin::encode_region(relaxed,binding,physical_names,cascading.selected,cascading.order,cascading.allocation,100));auto cascade_stream=take(bin2bin::decode(relaxed,cascade_bytes,100));CHECK(cascade_stream[0].bytes.size()==2&&cascade_stream[1].bytes.size()==2&&cascade_stream[0].branch_target==110&&execute(relaxed,cascade_bytes,100)==42);
  cascade.blocks[3].successors={0};cascade.nodes[6].op="jump";cascade.nodes[6].inputs.clear();cascade.nodes[6].control=schedrow::ControlFlow::Branch;cascade.nodes[6].block_targets={0};
  auto backward=take(run_pipeline(cascade,no_spill,encoded));auto backward_stream=take(bin2bin::decode(relaxed,take(bin2bin::encode_region(relaxed,binding,physical_names,backward.selected,backward.order,backward.allocation,100)),100));CHECK(backward_stream[6].bytes.size()==2&&backward_stream[6].branch_target==100);
  auto loop=cfg;loop.blocks[1].successors={9};loop.nodes[3].op="jump";loop.nodes[3].inputs.clear();loop.nodes[3].control=schedrow::ControlFlow::Branch;loop.nodes[3].block_targets={9};take(run_pipeline(loop,target));
  // Shared expressions are forest boundaries; BURS emits them exactly once.
  unisel::Program dag{{{10,"const",{},7,"i64"},{20,"const",{},14,"i64"},{30,"add",{10,20},{},"i64"},{40,"add",{30,30},{},"i64"},{50,"return",{40},{},"",0,true,false,false}}, {40}};dag.nodes.back().control=schedrow::ControlFlow::Return;
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS})for(auto allocator:{AllocationStrategy::LinearScan,AllocationStrategy::Greedy,AllocationStrategy::GraphColoring,AllocationStrategy::Constraint}){
    auto options=encoded;options.selector=selector;options.allocator=allocator;auto module=take(run_pipeline(dag,target,options));CHECK(module.materialized&&module.materialized->frame_size>0&&!module.materialized->slots.empty());CHECK(execute(decoder,module.encoded->bytes)==42);CHECK(module.materialized->allocation.spilled.empty());take(regtl::verify(module.materialized->problem,module.materialized->allocation));take(schedrow::verify(module.materialized->region,target.scheduling,module.materialized->scheduled));auto exchange=take(machineir_bridge::deserialize(module.machine_ir_exchange));CHECK(exchange.spill_slots.size()==module.materialized->slots.size());CHECK(take(machineir_bridge::serialize(exchange))==module.machine_ir_exchange);
  }
  auto grouped_spill_target=target;grouped_spill_target.grouping_adapter=[](const unisel::Program&,const schedrow::Region& region){schedrow::InstructionGroup group{88,schedrow::GroupKind::Adjacent,{},"constants"};for(auto& i:region.instructions)if(i.opcode=="CONST")group.members.push_back(i.id);return Result<std::vector<schedrow::InstructionGroup>>::ok({group});};
  auto recompute=target;recompute.instructions["CONST"].rematerializable=true;auto recomputed=take(run_pipeline(dag,recompute,encoded));auto stacked=take(run_pipeline(dag,target,encoded));
  CHECK(recomputed.materialized&&execute(decoder,recomputed.encoded->bytes)==42&&recomputed.materialized->slots.size()<stacked.materialized->slots.size());
  CHECK(std::any_of(recomputed.materialized->region.instructions.begin(),recomputed.materialized->region.instructions.end(),[](auto& i){return i.origin.starts_with("rematerialize");}));
   recompute.instructions["ADD"].rematerializable=true;auto expressions=take(run_pipeline(dag,recompute,encoded));CHECK(expressions.materialized&&execute(decoder,expressions.encoded->bytes)==42);
   auto trapping_recipe=recompute;trapping_recipe.instructions["ADD"].speculative=false;fails(run_pipeline(dag,trapping_recipe,encoded),Error::Code::Unsupported);
   auto kept_operand=target;kept_operand.instructions["ADD"].rematerializable=true;auto operand_program=dag;operand_program.nodes[3].inputs={10,30};auto kept_expression=take(run_pipeline(operand_program,kept_operand,encoded));CHECK(execute(decoder,kept_expression.encoded->bytes)==28);take(regtl::verify(kept_expression.allocation_problem,*kept_expression.allocation));
  auto grouped_spills=take(run_pipeline(dag,grouped_spill_target,encoded));CHECK(grouped_spills.materialized&&execute(decoder,grouped_spills.encoded->bytes)==42);CHECK(grouped_spills.materialized->region.groups.size()==1);take(schedrow::verify_order(grouped_spills.materialized->region,grouped_spills.order));take(schedrow::verify(grouped_spills.materialized->region,target.scheduling,grouped_spills.materialized->scheduled));
  auto strict=target;strict.burs_policy=limeburg::GraphPolicy::TreeOnly;encoded.selector=SelectionStrategy::BURS;fails(run_pipeline(dag,strict,encoded),Error::Code::Unsupported);
  auto no_scratch=target;no_scratch.spill_classes[0].scratch={1};no_scratch.instructions["ADD"].early_definitions={0};for(auto& n:dag.nodes)if(n.produces_value)no_scratch.constraints[n.id].forbidden={2,3};fails(run_pipeline(dag,no_scratch,encoded),Error::Code::Unsatisfiable);
  auto implicit=target;implicit.spill_classes.clear();implicit.instructions["ADD"].implicit_uses={0};auto physical=take(run_pipeline(dag,implicit,encoded));CHECK(physical.allocation->regs.at(10)!=0&&physical.allocation->regs.at(20)!=0&&physical.allocation->regs.at(30)!=0);CHECK(execute(decoder,physical.encoded->bytes)==42);implicit.instructions["ADD"].implicit_uses={100};fails(run_pipeline(dag,implicit,encoded),Error::Code::InvalidArgument);
  auto live_scratch=target;live_scratch.instructions["RETURN"].implicit_uses={1};auto preserved=take(run_pipeline(dag,live_scratch,encoded));CHECK(preserved.materialized&&execute(decoder,preserved.encoded->bytes)==42);for(auto [value,source]:preserved.materialized->value_sources)CHECK(preserved.materialized->allocation.regs.at(value)!=1);
  auto wrong_transfer=target;wrong_transfer.instructions["RELOAD"].access->read=false;wrong_transfer.instructions["RELOAD"].access->write=true;fails(run_pipeline(dag,wrong_transfer,encoded),Error::Code::Conflict);
  auto slow_reload=target;slow_reload.instructions["RELOAD"].result_latencies={{0,5}};auto slow=take(run_pipeline(dag,slow_reload,encoded));CHECK(execute(decoder,slow.encoded->bytes)==42);for(auto& i:slow.materialized->region.instructions)if(i.opcode=="RELOAD")CHECK(i.result_latency.at(i.defs[0])==5);take(schedrow::verify(slow.materialized->region,slow_reload.scheduling,slow.materialized->scheduled));
  slow_reload.instructions["RELOAD"].result_latencies={{1,5}};fails(run_pipeline(dag,slow_reload,encoded),Error::Code::Conflict);
  auto constrained_transfer=target;constrained_transfer.instructions["SPILL"].fixed_uses={{0,1}};constrained_transfer.instructions["RELOAD"].implicit_uses={3};constrained_transfer.instructions["RELOAD"].implicit_defs={1};constrained_transfer.instructions["RELOAD"].early_definitions={0};
  auto constrained_spills=take(run_pipeline(dag,constrained_transfer,encoded));CHECK(execute(decoder,constrained_spills.encoded->bytes)==42);
  for(auto& i:constrained_spills.materialized->region.instructions){if(i.opcode=="SPILL")CHECK(constrained_spills.materialized->allocation.regs.at(i.uses[0])==1);if(i.opcode=="RELOAD"){CHECK(i.early_defs==i.defs&&i.implicit_uses==std::vector<uint32_t>{3}&&i.implicit_defs==std::vector<uint32_t>{1});CHECK(constrained_spills.materialized->allocation.regs.at(i.defs[0])==2);}}
  constrained_transfer.spill_options.work_limit=0;fails(run_pipeline(dag,constrained_transfer,encoded),Error::Code::ResourceLimit);
  auto bad_transfer=target;bad_transfer.spill_options.configure_transfer=[](auto& i){i.access->address_space="user-memory";return Result<std::map<regtl::VReg,regtl::Constraint>>::ok({});};fails(run_pipeline(dag,bad_transfer,encoded),Error::Code::Conflict);
  // An explicit ABI adapter imports a spilled live-in from r0 into the private
  // frame, then reloads it for the terminating return. The final graph and its
  // allocation are verified independently of the adapter's semantic proof.
  unisel::Program boundary_program{{{7,"input",{}, {},"i64",0,false},{0,"return",{7}, {},"",0,true,false,false}}, {}};boundary_program.nodes.back().control=schedrow::ControlFlow::Return;
  auto boundary_target=target;regtl::SpillAdapter boundary_adapter;
  boundary_adapter.lower=[](const auto& region,auto,const auto& problem,const auto&,auto classes,auto)->Result<regtl::AllocatedRegion>{
    regtl::AllocatedRegion result;result.region=region;auto& c=classes[0];result.frame_size=c.size;result.slots={{7,c.klass,0,c.size,c.alignment}};result.allocation.regs={{7,0},{8,1}};result.value_sources={{8,7}};
    schedrow::Instruction store{};store.id=1;store.opcode=c.store_opcode;store.uses={7};store.immediates={{7,0}};store.memory=true;store.speculative=false;store.access=schedrow::MemoryAccess{false,true,false,false,schedrow::MemoryOrdering::Relaxed,c.address_space,{},c.size,c.alignment};
    auto load=store;load.id=2;load.opcode=c.load_opcode;load.uses.clear();load.defs={8};load.access->read=true;load.access->write=false;result.region.instructions.front().uses={8};result.region.instructions.insert(result.region.instructions.begin(),{store,load});result.order={1,2,0};
    regtl::Function function;function.classes=problem.classes;function.values={{7,c.klass,{{},{},0},false},{8,c.klass,{{},{},1},false}};function.blocks={{0,{{1,{}, {7}},{2,{8},{}},{0,{}, {8}}}}};auto analyzed=regtl::analyze(function);if(!analyzed)return Result<regtl::AllocatedRegion>::err(analyzed.error());result.problem=std::move(analyzed.value().problem);return Result<regtl::AllocatedRegion>::ok(std::move(result));
  };
  boundary_adapter.prove=[](const auto& source,const auto& allocation,const auto& lowered){if(source.instructions.size()!=1||source.instructions[0].control!=schedrow::ControlFlow::Return||source.instructions[0].uses!=std::vector<uint32_t>{7}||allocation.spilled!=std::vector<uint32_t>{7}||lowered.region.instructions.size()!=3)return Result<int>::err({Error::Code::Conflict,"ABI transfer proof rejected"});return Result<int>::ok(0);};
  boundary_target.spill_adapter=boundary_adapter;auto boundary_options=encoded;boundary_options.allocator=AllocationStrategy::PBQP;boundary_options.pbqp.costs.values={{7,1,{{0,100},{1,100},{2,100},{3,100}}}};
  auto boundary_module=take(run_pipeline(boundary_program,boundary_target,boundary_options));CHECK(boundary_module.allocation->spilled==std::vector<uint32_t>{7}&&execute(decoder,boundary_module.encoded->bytes,0,{{"r0",42}})==42);
  boundary_target.spill_adapter->prove={};fails(run_pipeline(boundary_program,boundary_target,boundary_options),Error::Code::InvalidArgument);
  boundary_target.spill_adapter=boundary_adapter;boundary_target.spill_adapter->prove=[](const auto&,const auto&,const auto&){return Result<int>::err({Error::Code::Conflict,"rejected semantics"});};fails(run_pipeline(boundary_program,boundary_target,boundary_options),Error::Code::Conflict);
  boundary_target.spill_adapter=boundary_adapter;boundary_target.spill_adapter->lower=[lower=boundary_adapter.lower](const auto& r,auto o,const auto& p,const auto& a,auto c,auto outputs){auto result=lower(r,o,p,a,c,outputs);if(result)result.value().allocation.regs[8]=0;return result;};fails(run_pipeline(boundary_program,boundary_target,boundary_options),Error::Code::Conflict);
  auto timed_target=grouped_spill_target;timed_target.instructions["CONST"].latency_range=schedrow::LatencyRange{1,4};timed_target.instructions["CONST"].result_latency_ranges={{0,{1,4}}};timed_target.instructions["CONST"].operand_latencies={{0,"ADD",0,{0,2},false},{0,"ADD",1,{1,3},false}};timed_target.instructions["RELOAD"].result_latency_ranges={{0,{2,5}}};auto timed_spills=take(run_pipeline(dag,timed_target,encoded));CHECK(timed_spills.materialized&&execute(decoder,timed_spills.encoded->bytes)==42);take(schedrow::verify(timed_spills.materialized->region,timed_target.scheduling,timed_spills.materialized->scheduled));for(auto& i:timed_spills.materialized->region.instructions)if(i.opcode=="RELOAD")CHECK(i.result_latency_ranges.at(i.defs[0])==schedrow::LatencyRange({2,5}));
  auto shared_constant=dag;shared_constant.nodes[2].inputs={10,10};shared_constant.nodes[3].inputs={30,20};CHECK(execute(decoder,take(run_pipeline(shared_constant,target,encoded)).encoded->bytes)==28);
  auto straight=take(run_pipeline(dag,no_spill,encoded));auto reordered=straight.order;std::swap(reordered[0],reordered[2]);fails(bin2bin::encode_region(decoder,binding,physical_names,straight.selected,reordered,straight.allocation),Error::Code::Conflict);
  const auto* source=R"(machine fixture {
    regclass G=[$r0,$r1];operator const(0);operator branch(1);operator return(1);
    instruction C {latency=0;}instruction B {latency=0;control_flow=conditional_branch;}instruction R {latency=0;control_flow=return;}
    pattern c: const():i64 -> C;
    pattern b: branch(?condition:i64) -> B side_effects true;
    pattern r: return(?value:i64) -> R side_effects true;
    default_register_class=G;
  }program graph {
    block entry {node %1=const(42):i64;node %2=branch(%1) produces_value false properties {control_flow=conditional_branch;targets=[left,right];};successors=[left,right];}
    block right {node %3=return(%1) produces_value false properties {control_flow=return;};}
    block left {node %4=return(%1) produces_value false properties {control_flow=return;};}
  })";
  auto document=take(unisel::load_umd(source));auto parsed=take(run_pipeline(*document.program,take(make_target(document.machine)),{true,true,true}));CHECK(parsed.selected.blocks.size()==3&&parsed.selected.instructions.size()==4);
  tunah::Session session;take(session.load_rules("(operator add_i64 2) (rule zero (add_i64 ?x 0) ?x)"));tunah::GraphAdapterOptions adapter;adapter.operators={{"add",{"add_i64","i64",2,true}}};unisel::Program stores{{{0,"input",{}, {},"i64",0,false},{90,"store",{0},{},"",0,true,true,false},{10,"store",{0},{},"",0,true,true,false}}, {}};stores.nodes[1].access=schedrow::MemoryAccess{false,true};stores.nodes[2].access=schedrow::MemoryAccess{false,true};auto kept=take(tunah::optimize_graph(stores,session,adapter));CHECK(kept.program.nodes[1].id==90&&kept.program.nodes[2].id==10);
  if(argc==3){auto timed_cfg_target=timed_target;timed_cfg_target.grouping_adapter={};std::ofstream cfg_file(std::string(argv[2])+"/machineir-cfg.json"),spill_file(std::string(argv[2])+"/machineir-spill.json");CHECK(cfg_file.good()&&spill_file.good());cfg_file<<take(run_pipeline(cfg,timed_cfg_target,encoded)).machine_ir_exchange;spill_file<<timed_spills.machine_ir_exchange;CHECK(cfg_file.good()&&spill_file.good());}
  if(argc==4){std::map<uint32_t,std::string> names;for(auto& [name,id]:take(unisel::from_metacode(metadata)).physical_names)names[id]=name;auto bindings=take(bin2bin::encoding_bindings(metadata));for(int k=2;k<4;++k){std::ifstream file(argv[k]);CHECK(file.good());std::string json((std::istreambuf_iterator<char>(file)),{});auto x=take(machineir_bridge::deserialize(json));auto bytes=take(bin2bin::encode_region(decoder,bindings,names,x.region,x.order,x.allocation));CHECK(execute(decoder,bytes)==(k==2?43:44));if(k==2)CHECK(x.region.blocks[0].id==7);else CHECK(!x.spill_slots.empty()&&x.frame_size>0);}}
});}
