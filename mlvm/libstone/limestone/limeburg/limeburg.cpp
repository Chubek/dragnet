#include "limeburg.hpp"
#include "schedrow/operand_facts.hpp"
#include "schedrow/memory_metadata.hpp"
#include <limits>
#include <map>
#include <set>

namespace limestone::limeburg {
namespace {
bool legal_immediate(const Node& n,const std::optional<std::pair<int64_t,int64_t>>& range) {
  auto value=n.has_imm?std::optional<int64_t>(n.imm):n.known_constant;
  return !range||(value&&*value>=range->first&&*value<=range->second);
}
}
Result<int> validate(const RuleSet& rs) {
  std::unordered_set<uint32_t> nts;std::unordered_set<RuleId> ids;
  for(auto&[name,id]:rs.nonterminals)if(name.empty()||!nts.insert(id).second)return Result<int>::err({Error::Code::InvalidArgument,"invalid nonterminal identity"});
  std::function<bool(const Pattern&)> valid_pattern=[&](const Pattern& p) {
    if(p.immediate&&p.immediate->first>p.immediate->second)return false;
    if(!p.nonterminal.empty())return p.op.empty()&&p.children.empty()&&rs.nonterminals.contains(p.nonterminal);
    if(p.op.empty())return false;
    return std::all_of(p.children.begin(),p.children.end(),valid_pattern);
  };
  for(auto& r:rs.rules) {
    if(!ids.insert(r.id).second||r.cost<0||!rs.nonterminals.contains(r.lhs)||r.result!=r.lhs)return Result<int>::err({Error::Code::InvalidArgument,"invalid BURS rule: "+std::to_string(r.id)});
    if(r.immediate&&r.immediate->first>r.immediate->second)return Result<int>::err({Error::Code::InvalidArgument,"invalid immediate bounds"});
    if(r.pattern) {
      if(!valid_pattern(*r.pattern)||!r.pattern->nonterminal.empty())return Result<int>::err({Error::Code::InvalidArgument,"invalid structured BURS pattern"});
    }else {
      if(r.op.empty())return Result<int>::err({Error::Code::InvalidArgument,"empty rule operator"});
      for(auto& nt:r.operands)if(!rs.nonterminals.contains(nt))return Result<int>::err({Error::Code::InvalidArgument,"unknown child nonterminal: "+nt});
    }
    std::vector<std::string> bindings;if(r.pattern){std::vector<const Pattern*> work{&*r.pattern};while(!work.empty()){auto pattern=work.back();work.pop_back();if(!pattern->binding.empty())bindings.push_back(pattern->binding);for(auto& child:pattern->children)work.push_back(&child);}}
    auto legal=metacode::validate_operand_constraints(r.constraints,bindings);if(!legal)return Result<int>::err({legal.error().code,(r.origin.empty()?"":r.origin+": ")+"BURS rule "+std::to_string(r.id)+": "+legal.error().message});
    legal=metacode::validate_host_constraints(r.host_constraints);if(!legal)return legal;
    if(r.fused_memory){if(!r.supports_side_effects||r.host_constraints.empty())return Result<int>::err({Error::Code::InvalidArgument,"memory fusion requires effect support and a named proof"});auto memory=schedrow::metadata::validate_memory_access(*r.fused_memory);if(!memory)return memory;}
  }
  return Result<int>::ok(0);
}
Result<StateTable> analyze(const std::vector<Node>& nodes,NodeId root,const RuleSet& rs,bool trace) {
  auto valid=validate(rs);if(!valid)return Result<StateTable>::err(valid.error());
  for(auto& rule:rs.rules)for(auto& predicate:rule.host_constraints)if(!predicate.prove)return Result<StateTable>::err({Error::Code::Unsupported,rule.origin+": unbound target predicate: "+predicate.name});
  std::map<NodeId,const Node*> map;
  for(auto& n:nodes) {
    if(n.op.empty()||!map.emplace(n.id,&n).second)return Result<StateTable>::err({Error::Code::InvalidArgument,"empty operation or duplicate tree node"});
    if(!n.required&&(!n.produces_value||!n.children.empty()||n.side_effect||n.access||n.call||n.terminator||n.may_trap))return Result<StateTable>::err({Error::Code::InvalidArgument,"external BURS values must be effect-free value leaves"});
    if(n.known_constant&&(n.required||n.has_imm))return Result<StateTable>::err({Error::Code::InvalidArgument,"known constants belong to non-immediate external boundaries"});
    auto metadata=metacode::load_operand_metadata(metacode::operand_metadata(n.metadata));if(!metadata)return Result<StateTable>::err(metadata.error());
    for(auto& argument:n.metadata.strings)if(n.required&&argument.index>=n.children.size()+n.metadata.strings.size()+n.has_imm)return Result<StateTable>::err({Error::Code::InvalidArgument,"invalid mixed BURS string argument"});
    if(n.access){auto memory=schedrow::metadata::validate_memory_access(*n.access);if(!memory)return Result<StateTable>::err(memory.error());}
  }
  if(!map.contains(root))return Result<StateTable>::err({Error::Code::InvalidArgument,"missing tree root"});
  std::unordered_map<NodeId,unsigned> state,parents;
  std::vector<std::pair<NodeId,bool>> stack{{root,false}};std::vector<NodeId> postorder;
  while(!stack.empty()) {
    auto [id,done]=stack.back();stack.pop_back();
    if(!map.contains(id))return Result<StateTable>::err({Error::Code::InvalidArgument,"unknown child node"});
    if(done){state[id]=2;postorder.push_back(id);continue;}
    if(state[id]==1)return Result<StateTable>::err({Error::Code::Conflict,"cyclic selection tree"});
    if(state[id]==2){if(!map.at(id)->required)continue;return Result<StateTable>::err({Error::Code::Unsupported,"shared DAG requires an explicit treeification policy"});}
    state[id]=1;stack.push_back({id,true});
    for(auto child:map.at(id)->children) {
      if(map.contains(child)&&!map.at(child)->produces_value)return Result<StateTable>::err({Error::Code::InvalidArgument,"BURS operand does not produce a value"});
      if(map.contains(child)&&++parents[child]>1&&map.at(child)->required)return Result<StateTable>::err({Error::Code::Unsupported,"shared DAG requires an explicit treeification policy"});
      stack.push_back({child,false});
    }
  }
  StateTable table{root};auto& dp=table.states;
  std::map<NonterminalId,std::string> names;if(trace)for(auto& [name,nt]:rs.nonterminals)names[nt]=name;
  auto available=[&](NodeId id){std::string out="{";for(auto& [nt,d]:dp[id]){if(out.back()!='{')out+=", ";out+=names.at(nt);}return out+"}";};
  auto rules=rs.rules;std::sort(rules.begin(),rules.end(),[](auto& a,auto& b){return std::tie(a.priority,a.id)<std::tie(b.priority,b.id);});
  for(auto id:postorder)for(auto& r:rules) {
    const auto& n=*map.at(id);
    auto reject=[&](std::string reason){if(trace)table.attempts.push_back({id,r.id,{},false,std::move(reason)});};
    if(r.external_only&&n.required){reject("rule requires an external value");continue;}
    if(r.instruction.empty()&&(!n.children.empty()||(n.required&&(!n.has_imm||n.side_effect||n.access||n.call||n.terminator||n.may_trap)))){reject("non-emitting rule requires an effect-free immediate or external leaf");continue;}
    if(!r.type.empty()&&r.type!=n.type){if(trace)reject("root type mismatch: expected "+r.type+", got "+n.type);continue;}
    if(!legal_immediate(n,r.immediate)){reject("root immediate outside legal range");continue;}
    Derivation d{r.id,r.cost,{},{},{}};int64_t cost=r.cost;
    std::map<std::string,NodeId> bindings;std::string reason;
    std::function<bool(NodeId,const Pattern&)> match=[&](NodeId nid,const Pattern& pattern) {
      const auto& node=*map.at(nid);
      auto mismatch=[&](auto message){if(trace)reason="node "+std::to_string(nid)+" ("+node.op+"): "+message();return false;};
      if(!pattern.type.empty()&&pattern.type!=node.type)return mismatch([&]{return "expected type "+pattern.type+", got "+node.type;});
      if(!pattern.register_class.empty()&&pattern.register_class!=node.register_class)return mismatch([&]{return "expected register class "+pattern.register_class+", got "+node.register_class;});
      if(!legal_immediate(node,pattern.immediate))return mismatch([&]{return "immediate outside ["+std::to_string(pattern.immediate->first)+", "+std::to_string(pattern.immediate->second)+"]";});
      if(!pattern.binding.empty()){auto [it,added]=bindings.emplace(pattern.binding,nid);if(!added&&it->second!=nid)return mismatch([&]{return "binding "+pattern.binding+" requires the same source value";});}
      if(!pattern.nonterminal.empty()) {
        auto it=dp[nid].find(rs.nonterminals.at(pattern.nonterminal));if(it==dp[nid].end())return mismatch([&]{return "cannot derive "+pattern.nonterminal+"; available "+available(nid);});
        cost+=it->second.cost;d.children.push_back(nid);d.child_nt.push_back(pattern.nonterminal);return true;
      }
      if(node.op!=pattern.op||node.children.size()!=pattern.children.size())return mismatch([&]{return "expected "+pattern.op+" with "+std::to_string(pattern.children.size())+" operands";});
      if(!node.required&&!r.instruction.empty())return mismatch([]{return "emitting rule cannot cover an external value";});
      if((node.side_effect||node.access||node.call||node.terminator||node.may_trap)&&!r.supports_side_effects)return mismatch([]{return "rule does not preserve observable effects";});
      d.covered.push_back(nid);
      for(size_t i=0;i<node.children.size();++i)if(!match(node.children[i],pattern.children[i]))return false;
      return true;
    };
    Pattern pattern;
    if(r.pattern)pattern=*r.pattern;
    else { pattern.op=r.op;for(auto& nt:r.operands)pattern.children.push_back(Pattern{"",nt}); }
    if(!match(id,pattern)){reject(std::move(reason));continue;}
    auto memory_count=std::count_if(d.covered.begin(),d.covered.end(),[&](auto value){return bool(map.at(value)->access);});
    if((memory_count>1&&!r.fused_memory)||(r.fused_memory&&!memory_count)){reject("multiple memory accesses require an explicit proved memory contract");continue;}
    bool legal=true;
    for(auto& constraint:r.constraints) {
      auto& operand=*map.at(bindings.at(constraint.operand));std::optional<metacode::BoundOperand> other;
      if(metacode::relational(constraint.predicate)){auto& node=*map.at(bindings.at(constraint.other));other=metacode::BoundOperand{node.id,node.has_imm?std::optional<int64_t>(node.imm):node.known_constant};}
      if(!metacode::satisfies(constraint,{operand.id,operand.has_imm?std::optional<int64_t>(operand.imm):operand.known_constant},other)){if(trace)reason="operand predicate "+std::string(metacode::predicate_name(constraint.predicate))+" failed for binding "+constraint.operand;legal=false;break;}
    }
    if(!legal){reject(std::move(reason));continue;}
    if(r.host_constraints.empty()&&std::any_of(d.covered.begin(),d.covered.end(),[&](auto value){return map.at(value)->required&&!map.at(value)->metadata.empty();})){reject("target-defined source properties require a proof predicate");continue;}
    if(!r.host_constraints.empty()){
      auto facts=[&](NodeId id){auto& node=*map.at(id);return schedrow::operand_facts(id,node.op,node.type,node.register_class,node.has_imm?std::optional<int64_t>(node.imm):node.known_constant,node.metadata,node.access,node.side_effect,node.call,node.terminator,node.may_trap,node.children);};
      metacode::Value::Object bound;for(auto& [name,id]:bindings)bound[name]=facts(id);metacode::Value::Array covered;for(auto id:d.covered)covered.push_back(facts(id));metacode::Value::Object context{{"root",facts(id)},{"bindings",metacode::Value(std::move(bound))},{"covered",metacode::Value(std::move(covered))}};if(r.fused_memory){context["memory_contract"]=schedrow::metadata::value(*r.fused_memory);metacode::Value::Array order;for(auto& source:nodes)if(std::find(d.covered.begin(),d.covered.end(),source.id)!=d.covered.end())order.push_back(facts(source.id));context["source_order"]=metacode::Value(std::move(order));}auto proved=metacode::prove_host_constraints(r.host_constraints,context);if(!proved)return Result<StateTable>::err(proved.error());if(!proved.value()){reject("target predicate rejected source facts");continue;}
    }
    if(cost>std::numeric_limits<int>::max())return Result<StateTable>::err({Error::Code::ResourceLimit,"BURS cost overflow"});
    d.cost=static_cast<int>(cost);
    auto nt=rs.nonterminals.at(r.lhs);auto it=dp[id].find(nt);bool improves=it==dp[id].end()||d.cost<it->second.cost;
    if(trace)table.attempts.push_back({id,r.id,d.cost,improves,improves?"improves state":"higher cost or lower-priority tie"});
    if(improves)dp[id][nt]=std::move(d);
  }
  for(auto id:postorder)dp.try_emplace(id);
  return Result<StateTable>::ok(std::move(table));
}
Result<Selection> select(const std::vector<Node>& nodes,NodeId root,const RuleSet& rs,std::string_view rootnt) {
  auto valid=validate(rs);if(!valid)return Result<Selection>::err(valid.error());
  auto nt=rs.nonterminals.find(std::string(rootnt));if(nt==rs.nonterminals.end())return Result<Selection>::err({Error::Code::InvalidArgument,"unknown root nonterminal"});
  auto analyzed=analyze(nodes,root,rs);if(!analyzed)return Result<Selection>::err(analyzed.error());auto& dp=analyzed.value().states;
  if(!dp.at(root).contains(nt->second)) {
    const auto& node=*std::find_if(nodes.begin(),nodes.end(),[&](auto& n){return n.id==root;});
    std::string message="node "+std::to_string(root)+" ("+node.op+") cannot derive "+std::string(rootnt)+"; available {";
    std::map<NonterminalId,std::string> names;for(auto& [name,id]:rs.nonterminals)names[id]=name;for(auto& [id,d]:dp.at(root)){if(message.back()!='{')message+=", ";message+=names.at(id);}message+='}';
    auto traced=analyze(nodes,root,rs,true);
    if(traced)for(auto& attempt:traced.value().attempts)if(attempt.node==root){auto& rule=*std::find_if(rs.rules.begin(),rs.rules.end(),[&](auto& r){return r.id==attempt.rule;});if(rule.lhs==rootnt){message+="\n  rule "+std::to_string(rule.id);if(!rule.origin.empty())message+=" @"+rule.origin;message+=": "+attempt.reason;}}
    return Result<Selection>::err({Error::Code::Unsatisfiable,std::move(message)});
  }
  Selection s{std::string(rootnt),{},dp.at(root).at(nt->second).cost,root};
  std::vector<std::pair<NodeId,std::string>> work{{root,std::string(rootnt)}};
  while(!work.empty()) {
    auto [id,nt]=work.back();work.pop_back();auto d=dp.at(id).at(rs.nonterminals.at(nt));s.chosen[id]=d;
    for(size_t i=0;i<d.children.size();++i)work.push_back({d.children[i],d.child_nt[i]});
  }
  return Result<Selection>::ok(std::move(s));
}
Result<std::string> print_analysis(const StateTable& table,const RuleSet& rs) {
  auto valid=validate(rs);if(!valid)return Result<std::string>::err(valid.error());
  std::map<NonterminalId,std::string> names;for(auto& [name,id]:rs.nonterminals)names[id]=name;
  std::map<RuleId,const Rule*> rules;for(auto& rule:rs.rules)rules[rule.id]=&rule;
  std::string out="root "+std::to_string(table.root)+"\n";
  for(auto& [node,states]:table.states) {
    out+="node "+std::to_string(node)+":\n";
    if(states.empty())out+="  no derivations\n";
    for(auto& [nt,d]:states) {
      if(!names.contains(nt)||!rules.contains(d.rule)||rules.at(d.rule)->lhs!=names.at(nt)||d.cost<0)return Result<std::string>::err({Error::Code::InvalidArgument,"invalid BURS state in analysis dump"});
      out+="  "+names.at(nt)+" (#"+std::to_string(nt)+") cost "+std::to_string(d.cost)+" via rule "+std::to_string(d.rule)+" -> "+rules.at(d.rule)->instruction+"\n";
    }
  }
  for(auto& attempt:table.attempts) {
    if(!rules.contains(attempt.rule)||!table.states.contains(attempt.node)||(attempt.cost&&*attempt.cost<0))return Result<std::string>::err({Error::Code::InvalidArgument,"invalid BURS rule attempt in analysis dump"});
    auto& rule=*rules.at(attempt.rule);out+="node "+std::to_string(attempt.node)+", rule "+std::to_string(attempt.rule)+" ("+rule.lhs+")";
    if(!rule.origin.empty())out+=" @"+rule.origin;
    if(attempt.cost)out+=" cost "+std::to_string(*attempt.cost);
    out+=": "+attempt.reason+"\n";
  }
  return Result<std::string>::ok(std::move(out));
}
Result<schedrow::Region> emit_scheduler(const std::vector<Node>& nodes,const RuleSet& rs,const Selection& selection) {
  std::set<NodeId> roots;
  for(auto&[id,d]:selection.chosen)roots.insert(id);
  for(auto&[id,d]:selection.chosen)for(auto child:d.children)roots.erase(child);
  if(roots.size()!=1||(selection.root&&*selection.root!=*roots.begin()))return Result<schedrow::Region>::err({Error::Code::InvalidArgument,"selection must have one derivation root"});
  auto checked=select(nodes,*roots.begin(),rs,selection.root_nt);
  if(!checked)return Result<schedrow::Region>::err(checked.error());
  if(checked.value().cost!=selection.cost||checked.value().chosen!=selection.chosen)return Result<schedrow::Region>::err({Error::Code::Conflict,"selection does not match its BURS derivation"});
  std::map<NodeId,const Node*> map;for(auto& n:nodes)map[n.id]=&n;
  std::map<RuleId,const Rule*> rules;for(auto& r:rs.rules)rules[r.id]=&r;
  schedrow::Region region{"limeburg"};
  std::vector<NodeId> ids;
  std::vector<std::pair<NodeId,bool>> work{{*roots.begin(),false}};std::set<NodeId> emitted;
  while(!work.empty()) {
    auto [id,done]=work.back();work.pop_back();
    if(done){ids.push_back(id);continue;}
    if(!emitted.insert(id).second)continue;
    work.emplace_back(id,true);auto& children=selection.chosen.at(id).children;
    for(auto child=children.rbegin();child!=children.rend();++child)work.emplace_back(*child,false);
  }
  for(auto id:ids) {
    const auto& d=selection.chosen.at(id);
    if(!map.contains(id)||!rules.contains(d.rule))return Result<schedrow::Region>::err({Error::Code::InvalidArgument,"invalid selected derivation"});
    const auto& rule=*rules.at(d.rule);
     if(rule.instruction.empty()) {
       if((!map.at(id)->has_imm&&map.at(id)->required)||!map.at(id)->children.empty())return Result<schedrow::Region>::err({Error::Code::Unsupported,"non-emitting rules must lower immediate or external leaves"});
      if(id==*roots.begin())return Result<schedrow::Region>::err({Error::Code::Unsupported,"an immediate-only root has no machine instruction"});
      continue;
    }
     schedrow::Instruction i{};i.id=id;i.opcode=rule.instruction;if(map.at(id)->produces_value)i.defs={id};
     i.access=rule.fused_memory;i.memory=bool(i.access);
      if(map.at(id)->produces_value&&!map.at(id)->register_class.empty())i.register_classes[id]=map.at(id)->register_class;
    for(auto source:d.covered)if(map.at(source)->has_imm)i.immediates.emplace_back(source,map.at(source)->imm);
    for(auto child:d.children) {
       if(rules.at(selection.chosen.at(child).rule)->instruction.empty()){if(map.at(child)->has_imm)i.immediates.emplace_back(child,map.at(child)->imm);else i.uses.push_back(child);}
       else {i.uses.push_back(child);region.deps.push_back({child,id,schedrow::DepKind::True,0});}
     }
     for(auto child:i.uses)if(!map.at(child)->register_class.empty())i.register_classes[child]=map.at(child)->register_class;
     for(auto child:i.uses)if(!map.at(child)->metadata.empty())i.source_metadata[child]=map.at(child)->metadata;
      i.origin="rule "+std::to_string(d.rule)+", source "+std::to_string(id);
      if(!rule.origin.empty())i.origin+="; "+rule.origin;
     for(auto source:d.covered) {
       const auto& node=*map.at(source);
       if(!node.metadata.empty())i.source_metadata[source]=node.metadata;
       if(node.access){if(!rule.fused_memory){if(i.access)return Result<schedrow::Region>::err({Error::Code::Unsupported,"fused memory accesses require an effect adapter"});i.access=node.access;}i.memory=true;}
        i.call|=node.call;i.terminator|=node.terminator;i.may_trap|=node.may_trap;
        i.barrier|=node.side_effect&&!node.access&&!node.call&&!node.terminator;
       if(node.side_effect||node.access||node.call||node.terminator||node.may_trap)i.speculative=false;
       if(!node.origin.empty())i.origin+="; "+node.origin;
     }
    region.instructions.push_back(std::move(i));
  }
  auto valid=schedrow::validate_region(region);if(!valid)return Result<schedrow::Region>::err(valid.error());
  return Result<schedrow::Region>::ok(std::move(region));
}
}
