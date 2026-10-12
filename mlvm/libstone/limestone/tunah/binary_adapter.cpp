#include "binary_adapter.hpp"
#include <map>

namespace limestone::tunah {
namespace {
void field(std::string& identity,std::string_view value){identity+=std::to_string(value.size())+":";identity.append(value);}
std::string identity(const Session& session,const std::string& context,const Limits& limits,const CostModel& model) {
  // Construct a bounded, deterministic identity rather than hashing addresses or
  // hiding rule changes behind a caller's version string. Source locations do
  // not alter generated semantics and are retained in the owning session snapshot.
  std::string identity="tunah.binary:2:";field(identity,context);
  std::map<std::string,size_t> operators(session.operators().begin(),session.operators().end());field(identity,std::to_string(operators.size()));
  for(auto& [name,arity]:operators){field(identity,name);field(identity,std::to_string(arity));}
  auto rules=session.rules();std::sort(rules.begin(),rules.end(),[](auto& a,auto& b){return a.name<b.name;});field(identity,std::to_string(rules.size()));
  for(auto& rule:rules){field(identity,rule.name);field(identity,format_term(rule.lhs));field(identity,format_term(rule.rhs));field(identity,std::to_string(rule.conditions.size()));for(auto& condition:rule.conditions){field(identity,condition.predicate);field(identity,std::to_string(condition.arguments.size()));for(auto& term:condition.arguments)field(identity,format_term(term));}}
  std::map<std::string,size_t> costs(model.operators.begin(),model.operators.end());field(identity,std::to_string(model.literal));field(identity,std::to_string(costs.size()));for(auto [name,cost]:costs){field(identity,name);field(identity,std::to_string(cost));}
  for(auto n:{limits.iterations,limits.nodes,limits.classes})field(identity,std::to_string(n));field(identity,std::to_string(limits.time_ms));return identity;
}
}
Result<bin2bin::SemanticTransform> binary_transform(const Session& session,std::string context,const BinaryAdapterOptions& options) {
  if(context.empty())return Result<bin2bin::SemanticTransform>::err({Error::Code::InvalidArgument,"binary Tunah adapter needs a semantic/analysis context identity"});
  if(!options.legality)return Result<bin2bin::SemanticTransform>::err({Error::Code::Unsupported,"binary Tunah adapter needs a target legality analysis"});
  bin2bin::SemanticTransform transform;transform.identity=identity(session,context,options.limits,options.costs);
  transform.cacheable=!options.limits.time_ms&&!options.limits.cancelled;
  transform.apply=[snapshot=std::make_shared<const Session>(session),options](const bin2bin::LiftedInstruction& instruction)->Result<std::string> {
    if(instruction.status!=bin2bin::Status::Supported)return Result<std::string>::err({Error::Code::Unsupported,"cannot optimize unsupported lifted instruction"});
    auto source="<binary:"+std::to_string(instruction.address)+">";auto term=parse_term(instruction.semantics,source);if(!term)return Result<std::string>::err(term.error());
    auto ingress=options.legality(instruction,term.value());if(!ingress)return Result<std::string>::err(ingress.error());if(!ingress.value())return Result<std::string>::err({Error::Code::Conflict,source+": illegal input semantic term"});
    auto result=snapshot->saturate(term.value(),options.limits,options.costs);if(!result)return Result<std::string>::err(result.error());
    auto checked=options.legality(instruction,result.value().term);if(!checked)return Result<std::string>::err(checked.error());if(!checked.value())return Result<std::string>::err({Error::Code::Conflict,source+": illegal extracted semantic term"});
    return Result<std::string>::ok(result.value().expression);
  };
  return Result<bin2bin::SemanticTransform>::ok(std::move(transform));
}
Result<bin2bin::RegionTransform> binary_region_transform(const Session& session,std::string context,const BinaryRegionAdapterOptions& options) {
  if(context.empty()||!options.ingest||!options.legality||!options.reconstruct)return Result<bin2bin::RegionTransform>::err({Error::Code::InvalidArgument,"binary region adapter requires identity, ingress, legality and reconstruction"});
   bin2bin::RegionTransform transform;transform.identity="region:"+identity(session,context,options.limits,options.costs);transform.cacheable=!options.limits.time_ms&&!options.limits.cancelled;
   transform.source_state_model=options.source_state_model;transform.target_state_model=options.target_state_model;transform.source_domain=options.source_domain;transform.target_domain=options.target_domain;
  transform.apply=[snapshot=std::make_shared<const Session>(session),options](const bin2bin::SemanticRegion& input)->Result<bin2bin::SemanticRegion>{
    auto term=options.ingest(input);if(!term)return Result<bin2bin::SemanticRegion>::err(term.error());auto legal=options.legality(input,term.value());if(!legal)return Result<bin2bin::SemanticRegion>::err(legal.error());if(!legal.value())return Result<bin2bin::SemanticRegion>::err({Error::Code::Conflict,"illegal input binary region term"});
    auto optimized=snapshot->saturate(term.value(),options.limits,options.costs);if(!optimized)return Result<bin2bin::SemanticRegion>::err(optimized.error());legal=options.legality(input,optimized.value().term);if(!legal)return Result<bin2bin::SemanticRegion>::err(legal.error());if(!legal.value())return Result<bin2bin::SemanticRegion>::err({Error::Code::Conflict,"illegal extracted binary region term"});return options.reconstruct(input,optimized.value().term);
  };
  return Result<bin2bin::RegionTransform>::ok(std::move(transform));
}
}
