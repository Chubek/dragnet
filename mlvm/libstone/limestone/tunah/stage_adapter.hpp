#pragma once
#include "unisel_adapter.hpp"
#include "limeburg/limeburg.hpp"
#include "regtl/regtl.hpp"
#include "traceml/traceml.hpp"
#include "metacode/machine-ir/bridge.hpp"

namespace limestone::tunah {
struct LimeburgForest { std::vector<limeburg::Node> nodes; limeburg::NodeId root=0; };
// Concrete structural verifiers. Semantic vocabularies, target lowering, and
// preservation of stage-specific metadata remain explicit host contracts.
Result<int> verify_stage(const LimeburgForest&);
Result<int> verify_stage(const schedrow::Region&);
Result<int> verify_stage(const regtl::Function&);
Result<int> verify_stage(const traceml::Program&);
Result<int> verify_stage(const machineir_bridge::RegionExchange&);

template<class Stage> struct StageContract {
  std::string identity;
  std::function<Result<Term>(const Stage&)> ingest;
  std::function<Result<bool>(const Stage&,const Term&)> legal;
  std::function<Result<Stage>(const Stage&,const Term&)> reconstruct;
  // Prove that effects, boundaries, types, ABI constraints and provenance survive
  // reconstruction. This is mandatory, including on extraction-only runs.
  std::function<Result<int>(const Stage&,const Stage&)> preserve;
};
template<class Stage> struct StageOptimization { Stage stage; SaturationResult saturation; };

// Transactional lifecycle shared by the typed Limestone stage adapters. All
// callbacks are synchronous borrowed views; returned stages/derivations own data.
template<class Stage> Result<StageOptimization<Stage>> optimize_stage(const Stage& input,
    const Session& session,const StageContract<Stage>& contract,Limits limits={},const CostModel& costs={}) {
  using Output=Result<StageOptimization<Stage>>;
  if(contract.identity.empty()||!contract.ingest||!contract.legal||!contract.reconstruct||!contract.preserve)
    return Output::err({Error::Code::InvalidArgument,"incomplete semantic stage adapter contract"});
  try {
    auto valid=verify_stage(input);if(!valid)return Output::err(valid.error());
    auto term=contract.ingest(input);if(!term)return Output::err(term.error());
    auto legal=contract.legal(input,term.value());if(!legal)return Output::err(legal.error());
    if(!legal.value())return Output::err({Error::Code::Unsupported,"stage adapter rejected ingress: "+contract.identity});
    auto result=session.saturate(term.value(),std::move(limits),costs);if(!result)return Output::err(result.error());
    legal=contract.legal(input,result.value().term);if(!legal)return Output::err(legal.error());
    if(!legal.value())return Output::err({Error::Code::Conflict,"stage adapter rejected extraction: "+contract.identity});
    auto stage=contract.reconstruct(input,result.value().term);if(!stage)return Output::err(stage.error());
    valid=verify_stage(stage.value());if(!valid)return Output::err(valid.error());
    valid=contract.preserve(input,stage.value());if(!valid)return Output::err(valid.error());
    return Output::ok({std::move(stage.value()),std::move(result.value())});
  }catch(const Error& error){return Output::err(error);}
  catch(const std::exception& error){return Output::err({Error::Code::Internal,contract.identity+": "+error.what()});}
  catch(...){return Output::err({Error::Code::Internal,contract.identity+": stage adapter exception"});}
}

struct ForestOptimization {
  LimeburgForest forest;
  std::unordered_map<limeburg::NodeId,limeburg::NodeId> values;
  size_t rewrites=0;bool limit_reached=false;
};
// Built-in pure, typed forest adapter; effects and forest boundaries are pinned.
Result<ForestOptimization> optimize_forest(const LimeburgForest&,const Session&,const GraphAdapterOptions&);
}
