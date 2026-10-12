#include "detail.hpp"
#include <EquinoxNG.hpp>
#include <chrono>
#include <limits>
#include <map>

namespace limestone::tunah::detail {
// Equinox-NG accepts a host-defined cost type. Saturating arithmetic prevents
// its additive extractor from wrapping a large cost into a cheap alternative.
struct CheckedCost {
  size_t value=0;
  explicit constexpr operator size_t() const { return value; }
  constexpr bool operator==(const CheckedCost&) const = default;
  constexpr bool operator<(const CheckedCost& other) const { return value<other.value; }
  CheckedCost& operator+=(CheckedCost other) {
    const auto maximum=std::numeric_limits<size_t>::max();
    value=other.value>maximum-value?maximum:value+other.value;
    return *this;
  }
};
}
template<> struct std::numeric_limits<limestone::tunah::detail::CheckedCost> : std::numeric_limits<size_t> {
  static constexpr limestone::tunah::detail::CheckedCost max() noexcept { return {std::numeric_limits<size_t>::max()}; }
};

namespace limestone::tunah {
namespace {
using ETerm=equinoxng::Term;
ETerm convert(const Term& term) {
  if(term.constant) return ETerm::lit(equinoxng::Literal(*term.constant));
  if(term.op.front()=='?') return ETerm::var(term.op.substr(1));
  std::vector<ETerm> children;
  children.reserve(term.arguments.size());
  for(const auto& child:term.arguments) children.push_back(convert(child));
  return ETerm::op(term.op,std::move(children));
}
Term reconstruct(const ETerm& term) {
  Term result;
  if(term.is_lit()) result.constant=std::get<int64_t>(term.as_lit().value);
  else if(term.is_var()) result.op="?"+term.as_var().name;
  else {
    result.op=term.as_node().op.name;
    result.application=!term.as_node().children.empty();
    result.arguments.reserve(term.as_node().children.size());
    for(const auto& child:term.as_node().children) result.arguments.push_back(reconstruct(child));
  }
  return result;
}
size_t size(const ETerm& term) {
  size_t count=1;
  if(term.is_node()) for(const auto& child:term.as_node().children) count+=size(child);
  return count;
}
struct ExtractionCost {
  using cost_type=detail::CheckedCost;
  const CostModel& model;
  cost_type literal_cost(const equinoxng::Literal&) const { return {model.literal}; }
  cost_type symbol_cost(const equinoxng::Symbol& symbol, size_t arity) const {
    auto cost=model.operators.find(symbol.name);
    return {cost==model.operators.end()?1+arity:cost->second};
  }
};
}
struct Session::PreparedRules {
  struct Rule { size_t index; ETerm lhs, rhs; size_t growth; std::string trace_name; };
  std::vector<Rule> rules;
};
std::shared_ptr<const Session::PreparedRules> Session::prepare_rules(const std::vector<RewriteRule>& rules,
  const std::unordered_map<std::string,size_t>& operators) const {
  auto prepared=std::make_shared<PreparedRules>();
  std::set<std::string> names;
  for(size_t index=0;index<rules.size();++index) {
    const auto& rule=rules[index];
    if(!names.insert(rule.name).second) throw detail::diagnostic(Error::Code::InvalidArgument,"duplicate rule name: "+rule.name,rule.location);
    validate_rule(rule,operators);
    auto lhs=convert(rule.lhs),rhs=convert(rule.rhs);
    auto growth=size(lhs)+size(rhs);
    auto trace_name=rule.name;
    if(!rule.location.file.empty()&&rule.location.file!="<tunah>")
      trace_name+=" @"+rule.location.file+(rule.location.expanded?" [expanded]":"")+":"+std::to_string(rule.location.line)+":"+std::to_string(rule.location.column);
    prepared->rules.push_back({index,std::move(lhs),std::move(rhs),growth,std::move(trace_name)});
  }
  std::sort(prepared->rules.begin(),prepared->rules.end(),[&](const auto& a,const auto& b){return rules[a.index].name<rules[b.index].name;});
  return prepared;
}
Result<SaturationResult> Session::saturate(std::string_view root, Limits limits, const CostModel& costs) const {
  auto parsed=parse_term(root);
  if(!parsed) return Result<SaturationResult>::err(parsed.error());
  return saturate(parsed.value(),std::move(limits),costs);
}
Result<SaturationResult> Session::saturate(const Term& root, Limits limits, const CostModel& costs) const {
  try {
    std::set<std::string> variables;
    detail::validate_term(root,operators_,false,variables);
    if(costs.literal==std::numeric_limits<size_t>::max()) throw Error{Error::Code::InvalidArgument,"literal cost is reserved for infinity"};
    std::map<std::string,size_t> ordered_costs(costs.operators.begin(),costs.operators.end());
    for(const auto& [op,cost]:ordered_costs) {
      if(!operators_.contains(op)) throw Error{Error::Code::InvalidArgument,"cost for undeclared operator: "+op};
      if(cost==std::numeric_limits<size_t>::max()) throw Error{Error::Code::InvalidArgument,"operator cost is reserved for infinity: "+op};
    }
    auto input=convert(root);
    const auto input_size=size(input);
    if(!limits.nodes||!limits.classes||input_size>limits.nodes||input_size>limits.classes)throw Error{Error::Code::ResourceLimit,"input exceeds e-graph budget"};
    equinoxng::EGraph graph;SaturationResult result;const RewriteRule* active_rule=nullptr;const SourceLocation* input_location=&root.location;
    std::vector<std::pair<std::string,uint32_t>> active_bindings;
    std::map<uint32_t,size_t> input_facts;
    if(limits.trace)graph.set_observers([&](auto id,auto owner,const equinoxng::ENode& node){DerivationNode fact;fact.id=id.value;fact.eclass=owner.value;fact.location=active_rule?active_rule->location:*input_location;fact.origins.push_back(fact.location);fact.rule=active_rule?active_rule->name:"";if(node.is_literal())fact.constant=std::get<int64_t>(node.literal().value);else fact.op=node.symbol().name;for(auto child:node.children)fact.children.push_back(child.value);if(!active_rule)input_facts[owner.value]=result.derivation_nodes.size();result.derivation_nodes.push_back(std::move(fact));},
      [&](auto lhs,auto rhs,auto merged,auto left,auto right){DerivationStep step{lhs.value,rhs.value,merged.value,active_rule?active_rule->name:"",active_rule?active_bindings:std::vector<std::pair<std::string,uint32_t>>{},active_rule?active_rule->location:root.location};if(left)step.lhs_node=left->value;if(right)step.rhs_node=right->value;result.derivation_steps.push_back(std::move(step));});
    const auto start=std::chrono::steady_clock::now();
    bool stopped_latched=false;
    auto stopped=[&]{
      if(stopped_latched)return true;
      return stopped_latched=(limits.cancelled&&limits.cancelled())||(limits.time_ms&&
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count())>=limits.time_ms);
    };
    auto prepared=prepared_?prepared_:prepare_rules(rules_,operators_);
    std::vector<equinoxng::RewriteRule> compiled;
    compiled.reserve(prepared->rules.size());
    const CostModel analysis_costs;
    for(const auto& pattern:prepared->rules) {
      const auto* origin=&rules_[pattern.index];
      const auto* trace_name=&pattern.trace_name;
      const size_t growth=pattern.growth;
      compiled.emplace_back(origin->name,pattern.lhs,pattern.rhs,[&,growth,origin,trace_name](const equinoxng::Subst& bindings){
        const auto& rule=*origin;
        if(stopped()) { result.limit_reached=true; return false; }
        std::map<std::string,Term> representatives;
        for(const auto& condition:rule.conditions) {
          std::vector<Term> arguments;
          for(const auto& argument:condition.arguments) {
            if(argument.constant) { arguments.push_back(argument); continue; }
            auto variable=argument.op.substr(1);
            auto found=representatives.find(variable);
            if(found==representatives.end()) {
              auto extracted=graph.extract(bindings.at(variable),ExtractionCost{analysis_costs});
              if(extracted.cost==std::numeric_limits<size_t>::max()) throw Error{Error::Code::Internal,"predicate binding extraction failed"};
              found=representatives.emplace(variable,reconstruct(extracted.term)).first;
            }
            arguments.push_back(found->second);
          }
          auto allowed=predicates_.at(condition.predicate).evaluate(arguments);
          if(!allowed) throw detail::diagnostic(allowed.error().code,"predicate "+condition.predicate+": "+allowed.error().message,rule.location);
          if(!allowed.value()) return false;
        }
        // A conservative reservation covers both sides instantiated by the
        // vendor engine. Subtraction avoids overflow in the budget check.
        if(stopped()||graph.num_enodes()>limits.nodes||growth>limits.nodes-graph.num_enodes()||
           graph.num_classes()>limits.classes||growth>limits.classes-graph.num_classes()) {
          result.limit_reached=true; return false;
        }
        if(limits.trace) {
          std::map<std::string,uint32_t> sorted;
          for(const auto& [key,id]:bindings) sorted[key]=graph.find(id).value;
          std::string step=*trace_name;
          for(const auto& [key,id]:sorted) step+=" ?"+key+"="+std::to_string(id);
          result.trace.push_back(std::move(step));
          active_bindings.assign(sorted.begin(),sorted.end());
        }
        return true;
      });
    }
    if(limits.cancelled||limits.time_ms)graph.set_interrupt(stopped);
    try {
    auto insert=[&](auto&& self,const Term& term)->equinoxng::EClassId {
      std::vector<equinoxng::EClassId> children;children.reserve(term.arguments.size());for(auto& child:term.arguments)children.push_back(self(self,child));
      input_location=&term.location;auto id=term.constant?graph.add_node(equinoxng::ENode{equinoxng::Literal(*term.constant)}):graph.add_node(equinoxng::ENode{equinoxng::Symbol{term.op},std::move(children)});
      if(limits.trace){auto& origins=result.derivation_nodes.at(input_facts.at(id.value)).origins;if(std::find(origins.begin(),origins.end(),term.location)==origins.end())origins.push_back(term.location);}return id;
    };
    // The ordinary insertion path avoids traversing/converting the checked
    // source term twice. Per-node origins are only collected with tracing.
    auto handle=limits.trace?insert(insert,root):graph.add(input);result.root_class=handle.value;
    for(size_t iteration=0;iteration<limits.iterations;++iteration) {
      if(stopped()){result.limit_reached=true;break;}
      auto before=graph.num_enodes();size_t applied=0;++result.iterations;
      for(size_t k=0;k<compiled.size();++k){active_rule=&rules_[prepared->rules[k].index];applied+=graph.apply_rule(compiled[k]);active_rule=nullptr;graph.rebuild();if(result.limit_reached)break;}
      if(result.limit_reached)break;
      if(!applied&&before==graph.num_enodes()){result.saturated=true;break;}
    }
    if(!result.saturated&&result.iterations==limits.iterations)result.limit_reached=true;
    graph.rebuild();auto extracted=graph.extract(handle,ExtractionCost{costs});
    if(extracted.cost==std::numeric_limits<size_t>::max())throw Error{Error::Code::ResourceLimit,"extraction cost exceeds the representable range"};
    result.term=reconstruct(extracted.term); result.term.location=root.location;
    result.expression=format_term(result.term);result.cost=extracted.cost;result.nodes=graph.num_enodes();result.classes=graph.num_classes();
    result.extracted_class=graph.find(handle).value;
    }catch(const equinoxng::OperationInterrupted&) {
      // A interrupted vendor operation can leave an unfinished rebuild. The
      // graph is discarded; the original checked term is a proved-equivalent
      // fallback and avoids another unbounded vendor extraction after stopping.
      result.limit_reached=true;result.saturated=false;result.graph_discarded=true;result.term=root;result.expression=format_term(root);
      auto cost=[&](auto&& self,const ETerm& term)->detail::CheckedCost{if(term.is_lit())return {costs.literal};detail::CheckedCost value=ExtractionCost{costs}.symbol_cost(term.as_node().op,term.as_node().children.size());for(auto& child:term.as_node().children)value+=self(self,child);return value;};result.cost=cost(cost,input).value;
      if(result.cost==std::numeric_limits<size_t>::max())throw Error{Error::Code::ResourceLimit,"fallback extraction cost exceeds the representable range"};
      result.nodes=graph.num_enodes();result.classes=graph.num_classes();
    }
    result.rewrites=graph.stats().rewrites_applied;
    return Result<SaturationResult>::ok(std::move(result));
  }catch(const Error& e){return Result<SaturationResult>::err(e);}catch(const std::exception& e){return Result<SaturationResult>::err({Error::Code::Internal,e.what()});}
}
std::string print_derivation(const SaturationResult& result) {
  std::string out;
  for(auto& node:result.derivation_nodes){out+="node "+std::to_string(node.id)+" class "+std::to_string(node.eclass)+" = "+(node.constant?std::to_string(*node.constant):node.op);for(auto child:node.children)out+=" @"+std::to_string(child);out+=" by "+(node.rule.empty()?"input":node.rule)+"\n";}
  for(auto& step:result.derivation_steps){out+="merge @"+std::to_string(step.lhs)+" @"+std::to_string(step.rhs)+" -> @"+std::to_string(step.result)+" by "+(step.rule.empty()?"congruence":step.rule);if(step.lhs_node&&step.rhs_node)out+=" nodes "+std::to_string(*step.lhs_node)+" "+std::to_string(*step.rhs_node);for(auto& [name,id]:step.bindings)out+=" ?"+name+"=@"+std::to_string(id);out+='\n';}
  if(result.graph_discarded)out+="fallback original; interrupted graph discarded\n";
  else if(result.root_class&&result.extracted_class)out+="root @"+std::to_string(*result.root_class)+" extracted @"+std::to_string(*result.extracted_class)+"\n";
  return out;
}
}
