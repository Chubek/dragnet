#include "stage_adapter.hpp"
#include <map>

namespace limestone::tunah {
namespace {
unisel::Program graph(const LimeburgForest& forest) {
  unisel::Program result;result.outputs={forest.root};
  for(auto& n:forest.nodes) {
    unisel::Node node{n.id,n.op,n.children,n.has_imm?std::optional<int64_t>{n.imm}:std::nullopt,n.type};
    node.required=n.required;node.produces_value=n.produces_value;node.side_effect=n.side_effect;
    node.access=n.access;node.call=n.call;node.terminator=n.terminator;node.may_trap=n.may_trap;
    node.origin=n.origin;node.register_class=n.register_class;result.nodes.push_back(std::move(node));
  }
  return result;
}
}
Result<int> verify_stage(const LimeburgForest& input){return unisel::validate(graph(input),{});}
Result<int> verify_stage(const schedrow::Region& input){return schedrow::validate_region(input);}
Result<int> verify_stage(const regtl::Function& input){auto result=regtl::analyze(input);if(!result)return Result<int>::err(result.error());return Result<int>::ok(0);}
Result<int> verify_stage(const traceml::Program& input){return traceml::verify(input);}
Result<int> verify_stage(const machineir_bridge::RegionExchange& input){auto result=machineir_bridge::serialize(input);if(!result)return Result<int>::err(result.error());return Result<int>::ok(0);}

Result<ForestOptimization> optimize_forest(const LimeburgForest& input,const Session& session,const GraphAdapterOptions& options) {
  auto optimized=optimize_graph(graph(input),session,options);if(!optimized)return Result<ForestOptimization>::err(optimized.error());
  auto& result=optimized.value();ForestOptimization output;output.forest.root=result.program.outputs.front();
  std::map<uint32_t,const limeburg::Node*> originals;for(auto& node:input.nodes)originals[node.id]=&node;
  for(auto& n:result.program.nodes) {
    limeburg::Node node{n.id,n.op,n.type,n.inputs,n.constant.value_or(0),bool(n.constant),n.required,n.produces_value,n.side_effect,n.access,n.call,n.terminator,n.may_trap,n.origin,n.register_class};
    auto original=originals.find(n.id);
    if(original!=originals.end()&&original->second->op==n.op&&original->second->children==n.inputs&&original->second->has_imm==bool(n.constant)&&(!n.constant||original->second->imm==*n.constant))node.known_constant=original->second->known_constant;
    output.forest.nodes.push_back(std::move(node));
  }
  output.values=std::move(result.values);output.rewrites=result.rewrites;output.limit_reached=result.limit_reached;
  auto valid=verify_stage(output.forest);if(!valid)return Result<ForestOptimization>::err(valid.error());
  return Result<ForestOptimization>::ok(std::move(output));
}
}
