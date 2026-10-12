#include "schedrow.hpp"

namespace limestone::schedrow {
LatencyRange latency_bounds(const Instruction& instruction) {
  return instruction.latency_range.value_or(LatencyRange{instruction.latency,instruction.latency});
}
LatencyRange latency_bounds(const Instruction& producer,ValueId value,bool implicit,const Instruction* consumer,uint32_t use_index) {
  if(consumer)for(auto& timing:producer.operand_latencies)
    if(timing.result==value&&timing.implicit==implicit&&timing.consumer_opcode==consumer->opcode&&timing.use_index==use_index)return timing.cycles;
  const auto& ranges=implicit?producer.implicit_result_latency_ranges:producer.result_latency_ranges;
  if(auto range=ranges.find(value);range!=ranges.end())return range->second;
  const auto& scalars=implicit?producer.implicit_result_latency:producer.result_latency;
  if(auto scalar=scalars.find(value);scalar!=scalars.end())return {scalar->second,scalar->second};
  return latency_bounds(producer);
}
}
