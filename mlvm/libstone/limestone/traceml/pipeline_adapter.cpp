#include "traceml.hpp"
#include <map>

namespace limestone::traceml {
PortableLowering lower_graph(int64_t value) {
  PortableLowering lowering;
  lowering.program={{{1,"const",{},value,"i64"},{2,"ret",{1},{},"",0,true,true,false}},{1},{}};
  lowering.program.nodes.back().control=schedrow::ControlFlow::Return;
  lowering.patterns={{1,"constant","const","const.i64",{},1},{2,"return","ret","ret",{"i64"},1,{},true}};
  schedrow::Instruction constant{};constant.opcode="const.i64";constant.opcode_class="pseudo";constant.semantic_class="integer";constant.latency=0;
  schedrow::Instruction ret{};ret.opcode="ret";ret.opcode_class="pseudo";ret.semantic_class="return";ret.barrier=true;ret.latency=0;ret.terminator=true;ret.control=schedrow::ControlFlow::Return;
  lowering.instruction_models={std::move(constant),std::move(ret)};return lowering;
}
Result<PortableLowering> lower_trace(const ExecutionResult& execution) {
  if(execution.trace.empty()||!execution.result_value)return Result<PortableLowering>::err({Error::Code::InvalidArgument,"execution has no recorded integer trace"});
  PortableLowering lowering;std::map<uint32_t,int64_t> values;uint64_t next=1;std::optional<uint32_t> ordered;
  for(auto& event:execution.trace)if(event.value)next=std::max(next,uint64_t(*event.value)+1);
  auto identity=[&]()->Result<uint32_t>{if(next>UINT32_MAX)return Result<uint32_t>::err({Error::Code::ResourceLimit,"trace graph identity overflow"});return Result<uint32_t>::ok(static_cast<uint32_t>(next++));};
  for(auto& event:execution.trace) {
    unisel::Node node{};node.type="i64";node.origin="TraceML "+std::to_string(event.line)+":"+std::to_string(event.column)+", step "+std::to_string(event.step);
    for(auto input:event.inputs)if(!values.contains(input))return Result<PortableLowering>::err({Error::Code::Conflict,"trace references an undefined value"});
    if(event.kind==TraceEvent::Kind::Integer||event.kind==TraceEvent::Kind::Primitive) {
      if(!event.value||!event.result||values.contains(*event.value))return Result<PortableLowering>::err({Error::Code::Conflict,"invalid trace value definition"});
      node.id=*event.value;values[node.id]=*event.result;
      if(event.kind==TraceEvent::Kind::Integer){if(!event.inputs.empty())return Result<PortableLowering>::err({Error::Code::Conflict,"trace literal has inputs"});node.op="const";node.constant=event.result;}
      else {
        const std::map<std::string,size_t> arities{{"add",2},{"sub",2},{"mul",2},{"neg",1},{"eq",2},{"lt",2}};
        if(!arities.contains(event.operation)||arities.at(event.operation)!=event.inputs.size())return Result<PortableLowering>::err({Error::Code::Conflict,"invalid trace primitive"});
        // Reuse the language evaluator to verify externally constructed traces.
        std::string expression="("+event.operation;for(auto input:event.inputs)expression+=" "+std::to_string(values.at(input));expression+=")";
        auto program=compile(expression);if(!program)return Result<PortableLowering>::err(program.error());auto result=evaluate(program.value());if(!result)return Result<PortableLowering>::err(result.error());
        if(result.value()!=*event.result)return Result<PortableLowering>::err({Error::Code::Conflict,"trace primitive result disagrees with language semantics"});
        node.op=event.operation;node.inputs=event.inputs;node.may_trap=event.operation!="eq"&&event.operation!="lt";
      }
    }else if(event.kind==TraceEvent::Kind::Branch) {
      if(event.inputs.size()!=1||!event.taken||bool(values.at(event.inputs[0]))!=*event.taken)return Result<PortableLowering>::err({Error::Code::Conflict,"invalid recorded branch guard"});
      auto id=identity();if(!id)return Result<PortableLowering>::err(id.error());node.id=id.value();node.op=*event.taken?"guard_nonzero":"guard_zero";node.inputs=event.inputs;node.type="";node.side_effect=true;node.produces_value=false;
    }else continue;
    if(node.may_trap||node.side_effect){if(ordered)lowering.program.dependencies.push_back({*ordered,node.id,schedrow::DepKind::Ordering,0});ordered=node.id;}
    lowering.program.nodes.push_back(std::move(node));
  }
  if(!values.contains(*execution.result_value)||values.at(*execution.result_value)!=execution.value)return Result<PortableLowering>::err({Error::Code::Conflict,"trace result is undefined or inconsistent"});
  auto id=identity();if(!id)return Result<PortableLowering>::err(id.error());lowering.program.nodes.push_back({id.value(),"ret",{*execution.result_value},{},"",0,true,true,false});
  lowering.program.nodes.back().control=schedrow::ControlFlow::Return;
  if(ordered)lowering.program.dependencies.push_back({*ordered,id.value(),schedrow::DepKind::Ordering,0});lowering.program.outputs={*execution.result_value};
  uint32_t rule=0;
  const std::map<std::string,size_t> operators{{"const",0},{"add",2},{"sub",2},{"mul",2},{"neg",1},{"eq",2},{"lt",2},{"guard_zero",1},{"guard_nonzero",1},{"ret",1}};
  for(auto& [op,arity]:operators) {
    auto instruction=op=="const"?"const.i64":op=="ret"?"ret":op.starts_with("guard_")?op:"checked."+op+".i64";
    lowering.patterns.push_back({rule++,op,op,instruction,std::vector<std::string>(arity,"i64"),1,{},true});
    schedrow::Instruction model{};model.opcode=instruction;model.latency=0;model.semantic_class=op=="ret"?"return":op.starts_with("guard_")?"guard":"integer";model.opcode_class="pseudo";model.terminator=op=="ret";lowering.instruction_models.push_back(std::move(model));
  }
  auto checked=unisel::validate(lowering.program,lowering.patterns);if(!checked)return Result<PortableLowering>::err(checked.error());return Result<PortableLowering>::ok(std::move(lowering));
}
}
