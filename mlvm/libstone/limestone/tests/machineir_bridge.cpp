#include "test.hpp"
#include "limestone.hpp"
#include "metacode/machine-ir/bridge.hpp"
#include <fstream>
#include <sstream>

int main(int argc,char** argv){return test_main([&]{
  using namespace limestone;
  auto module=take(run_pipeline("(add 20 22)"));
  auto exchange=take(machineir_bridge::deserialize(module.machine_ir_exchange));
  CHECK(exchange.target=="portable-machineir"&&exchange.region.instructions.size()==2);
  CHECK(take(machineir_bridge::serialize(exchange))==module.machine_ir_exchange);
  exchange.region.instructions[0].origin="file:\"quoted\"\nline\tcontrol";
  exchange.region.instructions[0].priority=-10;exchange.region.instructions[0].pressure_delta={{"G",-1}};
  exchange.region.instructions[0].resources={{"ALU",4,0.5,2,{"ALU","ALT"}}};
  exchange.region.instructions[0].access=schedrow::MemoryAccess{true,true,true,true,schedrow::MemoryOrdering::Sequential,"heap",{17,23},8,8};
  exchange.region.instructions[0].result_latency={{1,3}};exchange.region.instructions[0].immediates={{0,INT64_MIN},{0,INT64_MAX}};
  exchange.schedule.clear(); // The changed latency/resources require rescheduling.
  auto rich=take(machineir_bridge::serialize(exchange));CHECK(take(machineir_bridge::serialize(take(machineir_bridge::deserialize(rich))))==rich);
  auto grouped=exchange;grouped.region.groups={{5,schedrow::GroupKind::Adjacent,{1,2},"result","fixture","produce-return",2}};auto group_text=take(machineir_bridge::serialize(grouped));auto group_copy=take(machineir_bridge::deserialize(group_text));CHECK(group_copy.region.groups[0].pattern=="produce-return"&&group_copy.region.groups[0].benefit==2&&take(machineir_bridge::serialize(group_copy))==group_text);
  auto group_json=take(metacode::parse_json(group_text));auto& group_root=std::get<metacode::Value::Object>(group_json.data);CHECK(group_root.at("version").text()=="3");group_root["version"]=metacode::Value(uint64_t(2));fails(machineir_bridge::deserialize(take(metacode::print_json(group_json))),Error::Code::InvalidArgument);
  grouped.region.groups[0].members={1,999};fails(machineir_bridge::serialize(grouped),Error::Code::InvalidArgument);
  auto overlapping=group_copy;overlapping.region.groups.push_back({6,schedrow::GroupKind::Pair,{1,2}});auto overlap_text=take(machineir_bridge::serialize(overlapping));CHECK(take(machineir_bridge::deserialize(overlap_text)).region.groups.size()==2);
  auto reserved_frame=exchange;reserved_frame.frame_size=32;CHECK(take(machineir_bridge::deserialize(take(machineir_bridge::serialize(reserved_frame)))).frame_size==32);
  auto multi=exchange;multi.region.instructions[0].issue_width=2;multi.region.instructions[0].issue_slots={0,2};multi.schedule={{1,0,0,{}, {2}},{2,3,1}};auto multi_text=take(machineir_bridge::serialize(multi));auto multi_copy=take(machineir_bridge::deserialize(multi_text));CHECK(multi_copy.region.instructions[0].issue_width==2&&multi_copy.schedule[0].additional_slots==std::vector<uint32_t>{2});CHECK(take(machineir_bridge::serialize(multi_copy))==multi_text);multi.schedule[0].additional_slots={0};fails(machineir_bridge::serialize(multi),Error::Code::Conflict);
  auto corrupted=exchange;corrupted.order.push_back(99);fails(machineir_bridge::serialize(corrupted),Error::Code::Conflict);
  corrupted=exchange;corrupted.region.instructions[0].uses={999};fails(machineir_bridge::serialize(corrupted),Error::Code::NotFound);
  corrupted=exchange;corrupted.region.instructions[1].defs={1};fails(machineir_bridge::serialize(corrupted),Error::Code::Conflict);
  corrupted=exchange;corrupted.region.instructions[0].early_defs={999};fails(machineir_bridge::serialize(corrupted),Error::Code::Conflict);
  corrupted=exchange;corrupted.region.instructions[0].throughput=0;fails(machineir_bridge::serialize(corrupted),Error::Code::InvalidArgument);
  corrupted=exchange;corrupted.schedule={{1,0},{2,0}};fails(machineir_bridge::serialize(corrupted),Error::Code::Conflict);
  auto domains=take(machineir_bridge::deserialize(module.machine_ir_exchange));domains.region.instructions[0].latency=4;domains.region.instructions[0].result_latency={{1,0}};domains.region.instructions[0].implicit_defs={1};domains.region.instructions[1].implicit_uses={1};domains.schedule={{1,0},{2,2}};
  fails(machineir_bridge::serialize(domains),Error::Code::Conflict);domains.region.instructions[0].implicit_result_latency={{1,2}};auto domain_text=take(machineir_bridge::serialize(domains));CHECK(take(machineir_bridge::deserialize(domain_text)).region.instructions[0].implicit_result_latency.at(1)==2);
  auto bad_key=take(metacode::parse_json(domain_text));auto& root=std::get<metacode::Value::Object>(bad_key.data);auto& operations=std::get<metacode::Value::Array>(root.at("instructions").data);auto& producer=std::get<metacode::Value::Object>(operations[0].data);producer["result_latency"]=metacode::Value(metacode::Value::Object{{"01",metacode::Value(uint64_t(0))}});fails(machineir_bridge::deserialize(take(metacode::print_json(bad_key))),Error::Code::InvalidArgument);
  domains.region.instructions[0].issue_slots={0};domains.schedule[0].slot=1;fails(machineir_bridge::serialize(domains),Error::Code::Conflict);domains.schedule[0].slot=0;take(machineir_bridge::serialize(domains));
  domains.region.instructions[0].resources={{"ALU",1,1}};domains.schedule[0].resources={"missing"};fails(machineir_bridge::serialize(domains),Error::Code::Conflict);
  auto timed=take(machineir_bridge::deserialize(module.machine_ir_exchange));auto& timed_producer=timed.region.instructions[0];auto& timed_consumer=timed.region.instructions[1];timed_producer.latency_range=schedrow::LatencyRange{2,8};timed_producer.result_latency_ranges={{1,{2,6}}};timed_producer.implicit_defs={1};timed_producer.implicit_result_latency_ranges={{1,{1,5}}};timed_consumer.implicit_uses={1};timed_producer.operand_latencies={{1,timed_consumer.opcode,0,{0,2},false},{1,timed_consumer.opcode,0,{1,3},true}};timed.schedule={{1,0},{2,3}};auto timed_text=take(machineir_bridge::serialize(timed));auto timed_copy=take(machineir_bridge::deserialize(timed_text));CHECK(take(machineir_bridge::serialize(timed_copy))==timed_text&&timed_copy.region.instructions[0].operand_latencies.size()==2);
  auto timing_json=take(metacode::parse_json(timed_text));auto& timing_root=std::get<metacode::Value::Object>(timing_json.data);CHECK(timing_root.at("version").text()=="5");timing_root["version"]=metacode::Value(uint64_t(4));fails(machineir_bridge::deserialize(take(metacode::print_json(timing_json))),Error::Code::InvalidArgument);
  timed.schedule[1].cycle=2;fails(machineir_bridge::serialize(timed),Error::Code::Conflict);
  auto sourced=exchange;sourced.region.instructions[0].source_metadata[17]={{{0,"label"},{2,"quoted\"\ntext"}},{{"domain",metacode::Value(std::string("target"))}}};
  auto source_text=take(machineir_bridge::serialize(sourced));auto source_copy=take(machineir_bridge::deserialize(source_text));CHECK(take(machineir_bridge::serialize(source_copy))==source_text&&source_copy.region.instructions[0].source_metadata.at(17).strings[1].index==2);
  auto source_json=take(metacode::parse_json(source_text));auto& source_root=std::get<metacode::Value::Object>(source_json.data);CHECK(source_root.at("version").text()=="6");source_root["version"]=metacode::Value(uint64_t(5));fails(machineir_bridge::deserialize(take(metacode::print_json(source_json))),Error::Code::InvalidArgument);
  sourced.region.instructions[0].source_metadata.at(17).strings.push_back({2,"duplicate"});fails(machineir_bridge::serialize(sourced),Error::Code::InvalidArgument);
  auto coissue=take(machineir_bridge::deserialize(module.machine_ir_exchange));coissue.region.instructions[0].latency=0;coissue.schedule={{1,0},{2,0}};take(machineir_bridge::serialize(coissue));std::reverse(coissue.schedule.begin(),coissue.schedule.end());fails(machineir_bridge::serialize(coissue),Error::Code::Conflict);
  auto issue_order=take(machineir_bridge::deserialize(module.machine_ir_exchange));issue_order.schedule={{2,10},{1,0}};fails(machineir_bridge::serialize(issue_order),Error::Code::Conflict);
  auto cfg=take(machineir_bridge::deserialize(module.machine_ir_exchange));cfg.region.entry=7;cfg.region.blocks={{7,"entry",{9},{}},{9,"exit",{},{}}};cfg.region.instructions[0].block=7;cfg.region.instructions[1].block=9;take(machineir_bridge::serialize(cfg));std::reverse(cfg.schedule.begin(),cfg.schedule.end());fails(machineir_bridge::serialize(cfg),Error::Code::Conflict);
  auto json=take(metacode::parse_json(R"({"unicode":"\ud83d\ude00","max":18446744073709551615,"min":-9223372036854775808})"));
  CHECK(take(metacode::parse_json(take(metacode::print_json(json)))).text()==json.text());
  fails(metacode::parse_json("{\"a\":1,\"a\":2}"),Error::Code::Parse);fails(metacode::parse_json("[1,]"),Error::Code::Parse);
  fails(metacode::parse_json("01"),Error::Code::Parse);fails(metacode::parse_json("\"\\ud800\""),Error::Code::Parse);
  fails(metacode::parse_json("18446744073709551616"),Error::Code::Parse);fails(metacode::parse_json("null"),Error::Code::Unsupported);
  fails(metacode::parse_json("1.5"),Error::Code::Unsupported);
  if(argc==2){std::ifstream input(argv[1]);CHECK(input.good());std::ostringstream text;text<<input.rdbuf();auto returned=take(machineir_bridge::deserialize(text.str()));CHECK(returned.region.instructions[0].immediates[0].second==43);CHECK(returned.schedule.size()==2&&returned.outputs==std::vector<uint32_t>{1});CHECK(returned.region.groups.size()==1&&returned.region.groups[0].benefit==2);CHECK(returned.region.instructions[0].source_metadata.at(17).strings[0].value=="runtime label");}
});}
