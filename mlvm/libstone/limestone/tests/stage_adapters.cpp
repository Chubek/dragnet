#include "test.hpp"
#include "tunah/stage_adapter.hpp"
#include <map>

int main(){return test_main([]{
  using namespace limestone;using namespace tunah;
  Session session;take(session.load_rules("(operator add 2) (rule zero (add ?x 0) ?x)"));
  GraphAdapterOptions vocabulary;vocabulary.operators={{"add",{"add","i64",2}}};
  LimeburgForest forest{{{1,"input","i64",{},0,false,false},{2,"const","i64",{},0,true},{3,"add","i64",{1,2}}},3};
  forest.nodes[0].origin="source:4:2";forest.nodes[0].known_constant=42;
  auto simplified=take(optimize_forest(forest,session,vocabulary));CHECK(simplified.forest.root==1&&simplified.forest.nodes.size()==1&&simplified.values.at(3)==1);
  CHECK(simplified.forest.nodes[0].origin=="source:4:2"&&simplified.forest.nodes[0].known_constant==42);
  auto pinned=forest;pinned.nodes[2].may_trap=true;CHECK(take(optimize_forest(pinned,session,vocabulary)).forest.nodes.size()==3);
  auto invalid=forest;invalid.nodes[2].children={99};fails(optimize_forest(invalid,session,vocabulary),Error::Code::InvalidArgument);

  schedrow::Region region{"local"};schedrow::Instruction add{};add.id=8;add.opcode="ADD_ZERO";add.defs={2};add.uses={1};add.origin="stage:5:3";region.instructions={add};
  StageContract<schedrow::Region> scheduler;
  scheduler.identity="test:i64:add-zero:1";
  scheduler.ingest=[](const auto& input){if(input.instructions.size()!=1||input.instructions[0].opcode!="ADD_ZERO")return Result<Term>::err({Error::Code::Unsupported,"unregistered instruction"});return parse_term("(add input 0)","stage:5:3");};
  scheduler.legal=[](const auto&,const Term& term){auto spelling=format_term(term);return Result<bool>::ok(spelling=="(add input 0)"||spelling=="input");};
  scheduler.reconstruct=[](const auto& input,const Term& term){auto result=input;if(format_term(term)=="input"){result.instructions[0].opcode="COPY";result.instructions[0].latency=0;}return Result<schedrow::Region>::ok(std::move(result));};
  scheduler.preserve=[](const auto& before,const auto& after){if(before.instructions[0].defs!=after.instructions[0].defs||before.instructions[0].uses!=after.instructions[0].uses||before.instructions[0].origin!=after.instructions[0].origin)return Result<int>::err({Error::Code::Conflict,"lost stage boundary/provenance"});return Result<int>::ok(0);};
  auto optimized=take(optimize_stage(region,session,scheduler));CHECK(optimized.stage.instructions[0].opcode=="COPY"&&optimized.saturation.expression=="input");
  take(schedrow::verify(optimized.stage,{},take(schedrow::schedule(optimized.stage,{}))));
  Limits extraction;extraction.iterations=0;CHECK(take(optimize_stage(region,session,scheduler,extraction)).stage.instructions[0].opcode=="ADD_ZERO");
  auto broken=scheduler;broken.reconstruct=[](const auto& input,const auto&){auto result=input;result.instructions.push_back(result.instructions[0]);return Result<schedrow::Region>::ok(result);};fails(optimize_stage(region,session,broken),Error::Code::InvalidArgument);
  broken=scheduler;broken.preserve=[](const auto&,const auto&){return Result<int>::err({Error::Code::Conflict,"metadata lost"});};fails(optimize_stage(region,session,broken),Error::Code::Conflict);
  broken=scheduler;broken.legal=[](const auto&,const auto&){return Result<bool>::ok(false);};fails(optimize_stage(region,session,broken),Error::Code::Unsupported);
  broken=scheduler;broken.preserve={};fails(optimize_stage(region,session,broken),Error::Code::InvalidArgument);
  broken=scheduler;broken.ingest=[](const auto&)->Result<Term>{throw std::runtime_error("callback failure");};fails(optimize_stage(region,session,broken),Error::Code::Internal);
  CHECK(region.instructions[0].opcode=="ADD_ZERO");

  regtl::Function function;function.classes={{"G",{0,1}}};function.values={{1,"G"},{2,"G"}};function.blocks={{0,{{8,{2},{1}}},{},{2}}};function.blocks[0].instructions[0].opcode="ADD_ZERO";
  StageContract<regtl::Function> allocation;allocation.identity=scheduler.identity;
  allocation.ingest=[](const auto&){return parse_term("(add input 0)");};allocation.legal=[&](const auto&,const auto& term){return scheduler.legal(region,term);};
  allocation.reconstruct=[](const auto& input,const Term& term){auto result=input;if(format_term(term)=="input"){auto& instruction=result.blocks[0].instructions[0];instruction.opcode="COPY";instruction.transfers={{{regtl::TransferOperand::Kind::Virtual,2},{regtl::TransferOperand::Kind::Virtual,1}}};}return Result<regtl::Function>::ok(std::move(result));};
  allocation.preserve=[](const auto& before,const auto& after){if(before.values.size()!=after.values.size()||before.blocks[0].live_out!=after.blocks[0].live_out)return Result<int>::err({Error::Code::Conflict,"allocation boundaries changed"});return Result<int>::ok(0);};
  auto copied=take(optimize_stage(function,session,allocation));auto liveness=take(regtl::analyze(copied.stage));take(regtl::verify(liveness.problem,take(regtl::pbqp_allocate(liveness.problem))));
  std::map<uint32_t,int64_t> state{{1,42}};for(auto& transfer:copied.stage.blocks[0].instructions[0].transfers)state[transfer.destination.id]=state.at(transfer.source.id);CHECK(state.at(2)==42);

  auto source=take(traceml::compile("(add 42 0)"));StageContract<traceml::Program> language;language.identity="TraceML:closed-integer:1";
  language.ingest=[](const auto&){return parse_term("(add 42 0)","language.tml");};
  language.legal=[](const auto&,const auto& term){return Result<bool>::ok(format_term(term)=="(add 42 0)"||term.constant==42);};
  language.reconstruct=[](const auto& input,const auto& term){auto result=traceml::compile(format_term(term));if(result){result.value().forms[0]->offset=input.forms[0]->offset;result.value().forms[0]->line=input.forms[0]->line;result.value().forms[0]->column=input.forms[0]->column;}return result;};
  language.preserve=[](const auto& before,const auto& after){auto a=traceml::evaluate(before),b=traceml::evaluate(after);if(!a||!b||a.value()!=b.value())return Result<int>::err({Error::Code::Conflict,"language semantic mismatch"});return Result<int>::ok(0);};
  auto constant=take(optimize_stage(source,session,language));CHECK(take(traceml::evaluate(constant.stage))==42&&constant.stage.forms[0]->kind==traceml::Expr::Kind::Integer);

  machineir_bridge::RegionExchange exchange;exchange.target="fixture";exchange.region=region;exchange.order={8};exchange.outputs={2};exchange.values={{1,"i64","G"},{2,"i64","G"}};
  StageContract<machineir_bridge::RegionExchange> machine;machine.identity=scheduler.identity;
  machine.ingest=[&](const auto& input){return scheduler.ingest(input.region);};machine.legal=[&](const auto& input,const auto& term){return scheduler.legal(input.region,term);};
  machine.reconstruct=[&](const auto& input,const auto& term){auto region=scheduler.reconstruct(input.region,term);if(!region)return Result<machineir_bridge::RegionExchange>::err(region.error());auto result=input;result.region=std::move(region.value());return Result<machineir_bridge::RegionExchange>::ok(std::move(result));};
  machine.preserve=[&](const auto& before,const auto& after){if(before.order!=after.order||before.outputs!=after.outputs||before.values.size()!=after.values.size())return Result<int>::err({Error::Code::Conflict,"machine boundary changed"});return scheduler.preserve(before.region,after.region);};
  auto lowered=take(optimize_stage(exchange,session,machine));auto roundtrip=take(machineir_bridge::deserialize(take(machineir_bridge::serialize(lowered.stage))));CHECK(roundtrip.region.instructions[0].opcode=="COPY"&&roundtrip.outputs==exchange.outputs);
});}
