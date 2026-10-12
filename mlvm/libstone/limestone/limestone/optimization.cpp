#include "optimization_internal.hpp"
#include "c_api_internal.hpp"

struct limestone_optimization { limestone::tunah::SaturationResult result; std::string derivation; };
namespace {
using namespace limestone::c_api_internal;
using limestone::Error;
void require(const void* handle,const char* message) {
  if(!handle)throw Error{Error::Code::InvalidArgument,message};
}
void name(const char* value) {
  if(!value||!*value)throw Error{Error::Code::InvalidArgument,"empty optimizer symbol"};
}
struct CallbackOwner {
  void* userdata;
  limestone_optimizer_release release=nullptr;
  ~CallbackOwner()noexcept {if(release)try{release(userdata);}catch(...) {}}
};
void callback(limestone_status status,const limestone_error& error) {
  if(status==LIMESTONE_OK)return;
  if(status<LIMESTONE_INVALID_ARGUMENT||status>LIMESTONE_RESOURCE_LIMIT)
    throw Error{Error::Code::Internal,"optimizer callback returned an invalid status"};
  auto end=std::find(std::begin(error.message),std::end(error.message),'\0');
  throw Error{static_cast<Error::Code>(static_cast<int>(status)-1),
    end==std::begin(error.message)?"optimizer callback failed":std::string(std::begin(error.message),end)};
}
}
extern "C" void limestone_optimization_limits_default(limestone_optimization_limits* out) {
  if(out)*out={20,10000,10000,10000,0,1};
}
extern "C" limestone_optimizer* limestone_optimizer_create(limestone_error* error) {
  return boundary(error,[]()->limestone_optimizer* {return new limestone_optimizer;});
}
extern "C" void limestone_optimizer_destroy(limestone_optimizer* optimizer){delete optimizer;}
extern "C" limestone_status limestone_optimizer_define_operator(limestone_optimizer* optimizer,const char* symbol,size_t arity,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");name(symbol);checked(optimizer->session.define_operator(symbol,arity));return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_optimizer_load_rules(limestone_optimizer* optimizer,const char* source,const char* source_name,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");require(source,"null rewrite source");checked(optimizer->session.load_rules(source,source_name?source_name:"<tunah>"));return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_optimizer_load_rules_file(limestone_optimizer* optimizer,const char* path,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");name(path);checked(optimizer->session.load_rules_file(path));return LIMESTONE_OK;});
}
extern "C" size_t limestone_optimizer_rule_count(const limestone_optimizer* optimizer){return optimizer?optimizer->session.rules().size():0;}
extern "C" limestone_status limestone_optimizer_define_predicate(limestone_optimizer* optimizer,const char* symbol,size_t arity,limestone_optimizer_predicate predicate,void* userdata,limestone_optimizer_release release,limestone_error* error) {
  return boundary(error,[&](){
    require(optimizer,"null optimizer");name(symbol);if(!predicate)throw Error{Error::Code::InvalidArgument,"null optimizer predicate"};
    auto owner=std::make_shared<CallbackOwner>();owner->userdata=userdata;
    checked(optimizer->session.define_predicate(symbol,arity,[owner,predicate](std::span<const limestone::tunah::Term> arguments)->limestone::Result<bool> {
      std::vector<std::string> terms;for(auto& term:arguments)terms.push_back(limestone::tunah::format_term(term));
      std::vector<const char*> views;for(auto& term:terms)views.push_back(term.c_str());
      int proved=0;limestone_error error{};callback(predicate(views.data(),views.size(),&proved,owner->userdata,&error),error);
      if(proved!=0&&proved!=1)throw Error{Error::Code::InvalidArgument,"optimizer predicate returned a non-Boolean proof"};
      return limestone::Result<bool>::ok(proved!=0);
    }));
    owner->release=release;return LIMESTONE_OK;
  });
}
extern "C" limestone_status limestone_optimizer_set_literal_cost(limestone_optimizer* optimizer,size_t cost,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");optimizer->options.costs.literal=cost;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_optimizer_set_operator_cost(limestone_optimizer* optimizer,const char* symbol,size_t cost,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");name(symbol);if(!optimizer->session.operators().contains(symbol))throw Error{Error::Code::NotFound,"unknown optimizer operator"};optimizer->options.costs.operators[symbol]=cost;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_optimizer_set_limits(limestone_optimizer* optimizer,const limestone_optimization_limits* limits,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");limestone_optimization_limits defaults;limestone_optimization_limits_default(&defaults);auto& settings=limits?*limits:defaults;
    if(!settings.nodes||!settings.classes||!settings.reconstructed_nodes||(settings.trace!=0&&settings.trace!=1))throw Error{Error::Code::InvalidArgument,"invalid optimization limits"};
    optimizer->options.limits.iterations=settings.iterations;optimizer->options.limits.nodes=settings.nodes;optimizer->options.limits.classes=settings.classes;optimizer->options.limits.time_ms=settings.time_ms;optimizer->options.limits.trace=settings.trace!=0;optimizer->options.reconstructed_nodes=settings.reconstructed_nodes;return LIMESTONE_OK;
  });
}
extern "C" limestone_status limestone_optimizer_set_cancellation(limestone_optimizer* optimizer,limestone_optimizer_cancellation cancelled,void* userdata,limestone_optimizer_release release,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");if(!cancelled&&(userdata||release))throw Error{Error::Code::InvalidArgument,"userdata without a cancellation callback"};
    std::function<bool()> replacement;std::shared_ptr<CallbackOwner> owner;
    if(cancelled){owner=std::make_shared<CallbackOwner>();owner->userdata=userdata;replacement=[owner,cancelled]{int stopped=0;limestone_error error{};callback(cancelled(owner->userdata,&stopped,&error),error);if(stopped!=0&&stopped!=1)throw Error{Error::Code::InvalidArgument,"optimizer cancellation returned a non-Boolean value"};return stopped!=0;};}
    replacement.swap(optimizer->options.limits.cancelled);if(owner)owner->release=release;return LIMESTONE_OK;
  });
}
extern "C" limestone_status limestone_optimizer_define_graph_operator(limestone_optimizer* optimizer,const char* opcode,const char* term_operator,const char* type,int commutative,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");name(opcode);name(term_operator);name(type);
    auto found=optimizer->session.operators().find(term_operator);if(found==optimizer->session.operators().end())throw Error{Error::Code::NotFound,"graph term operator needs a signature"};
    if((commutative!=0&&commutative!=1)||(commutative&&found->second!=2)||optimizer->options.constant_opcode==opcode)throw Error{Error::Code::InvalidArgument,"invalid pure graph operator declaration"};
    for(auto& [source,model]:optimizer->options.operators)if(source!=opcode&&model.term_operator==term_operator)throw Error{Error::Code::Conflict,"ambiguous graph term operator"};
    auto& models=optimizer->options.operators;auto prior=models.find(opcode);
    if(prior!=models.end()&&(prior->second.term_operator!=term_operator||prior->second.type!=type||prior->second.arity!=found->second||prior->second.commutative!=bool(commutative)))throw Error{Error::Code::Conflict,"conflicting graph operator declaration"};
    if(prior==models.end())models.emplace(opcode,limestone::tunah::GraphOperator{term_operator,type,found->second,bool(commutative)});return LIMESTONE_OK;
  });
}
extern "C" limestone_status limestone_optimizer_set_constant_opcode(limestone_optimizer* optimizer,const char* opcode,limestone_error* error) {
  return boundary(error,[&](){require(optimizer,"null optimizer");name(opcode);if(optimizer->options.operators.contains(opcode))throw Error{Error::Code::Conflict,"constant opcode is a registered graph operator"};optimizer->options.constant_opcode=opcode;return LIMESTONE_OK;});
}
extern "C" limestone_optimization* limestone_optimizer_saturate(const limestone_optimizer* optimizer,const char* expression,const char* source_name,limestone_error* error) {
  return boundary(error,[&]()->limestone_optimization* {
    require(optimizer,"null optimizer");require(expression,"null optimization term");
    // Callbacks can destroy their source handle without invalidating a run.
    auto snapshot=*optimizer;auto term=checked(limestone::tunah::parse_term(expression,source_name?source_name:"<tunah>"));
    auto result=checked(snapshot.session.saturate(term,snapshot.options.limits,snapshot.options.costs));auto derivation=limestone::tunah::print_derivation(result);
    return new limestone_optimization{std::move(result),std::move(derivation)};
  });
}
extern "C" void limestone_optimization_destroy(limestone_optimization* result){delete result;}
extern "C" const char* limestone_optimization_expression(const limestone_optimization* result){return result?result->result.expression.c_str():nullptr;}
extern "C" limestone_status limestone_optimization_get_info(const limestone_optimization* result,limestone_optimization_info* info,limestone_error* error) {
  return boundary(error,[&](){require(result,"null optimization result");require(info,"null optimization inspection output");auto& r=result->result;*info={r.cost,r.rewrites,r.nodes,r.classes,r.iterations,int(r.saturated),int(r.limit_reached)};return LIMESTONE_OK;});
}
extern "C" size_t limestone_optimization_trace_count(const limestone_optimization* result){return result?result->result.trace.size():0;}
extern "C" const char* limestone_optimization_trace(const limestone_optimization* result,size_t index){return result&&index<result->result.trace.size()?result->result.trace[index].c_str():nullptr;}
extern "C" const char* limestone_optimization_derivation(const limestone_optimization* result){return result?result->derivation.c_str():nullptr;}
extern "C" int limestone_optimization_used_fallback(const limestone_optimization* result){return result&&result->result.graph_discarded;}

extern "C" limestone_binary_transform* limestone_optimizer_binary_transform(const limestone_optimizer* optimizer,const char* context,limestone_binary_legality legality,void* userdata,limestone_optimizer_release release,limestone_error* error) {
  return boundary(error,[&]()->limestone_binary_transform* {
    require(optimizer,"null binary optimizer");name(context);if(!legality)throw Error{Error::Code::Unsupported,"binary optimizer needs a legality analysis"};
    auto owner=std::make_shared<CallbackOwner>();owner->userdata=userdata;
    limestone::tunah::BinaryAdapterOptions options;options.limits=optimizer->options.limits;options.costs=optimizer->options.costs;
    options.legality=[owner,legality](const limestone::bin2bin::LiftedInstruction& input,const limestone::tunah::Term& term)->limestone::Result<bool> {
      limestone_binary_semantic_view view{input.address,input.semantics.c_str(),static_cast<limestone_binary_control_flow>(input.control),int(input.branch_target.has_value()),input.branch_target.value_or(0)};
      auto candidate=limestone::tunah::format_term(term);int proved=0;limestone_error error{};
      callback(legality(&view,candidate.c_str(),&proved,owner->userdata,&error),error);
      if(proved!=0&&proved!=1)throw Error{Error::Code::InvalidArgument,"binary legality returned a non-Boolean proof"};
      return limestone::Result<bool>::ok(proved!=0);
    };
    auto handle=std::make_unique<limestone_binary_transform>(limestone_binary_transform{checked(limestone::tunah::binary_transform(optimizer->session,context,options))});
    owner->release=release;return handle.release();
  });
}
extern "C" void limestone_binary_transform_destroy(limestone_binary_transform* transform){delete transform;}
extern "C" const char* limestone_binary_transform_identity(const limestone_binary_transform* transform){return transform?transform->transform.identity.c_str():nullptr;}
extern "C" int limestone_binary_transform_is_cacheable(const limestone_binary_transform* transform){return transform&&transform->transform.cacheable;}
