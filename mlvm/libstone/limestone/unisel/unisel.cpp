#include "unisel.hpp"
#include "schedrow/operand_facts.hpp"
#include "schedrow/memory_metadata.hpp"
#include <SatieCDCL.hpp>
#include <SatieFrontendOPB.hpp>
#include <limits>
#include <map>
#include <set>

namespace limestone::unisel {
PatternTree pattern_tree(const Pattern& pattern) {
  if(pattern.tree)return *pattern.tree;
  PatternTree tree;tree.op=pattern.root_op;
  for(auto& type:pattern.operands){PatternTree input;if(type!="v")input.type=type;tree.inputs.push_back(std::move(input));}
  return tree;
}
Result<int> validate(const Program& p,const std::vector<Pattern>& patterns) {
  std::map<NodeId,const Node*> nodes;
  for(auto& n:p.nodes)if(n.op.empty()||!nodes.emplace(n.id,&n).second)return Result<int>::err({Error::Code::InvalidArgument,"empty operation or duplicate source node"});
  std::map<NodeId,size_t> indegree;std::map<NodeId,std::vector<NodeId>> out;
  for(auto& n:p.nodes) {
    auto metadata=metacode::load_operand_metadata(metacode::operand_metadata(n.metadata));if(!metadata)return Result<int>::err(metadata.error());
    if(n.access){auto memory=schedrow::metadata::validate_memory_access(*n.access);if(!memory)return memory;}
    std::set<uint32_t> string_indices;for(auto& argument:n.metadata.strings)if(argument.index>=n.inputs.size()+n.metadata.strings.size()+bool(n.constant)||!string_indices.insert(argument.index).second||argument.value.find('\0')!=std::string::npos)return Result<int>::err({Error::Code::InvalidArgument,"invalid mixed string argument at node "+std::to_string(n.id)});
    indegree[n.id]=n.inputs.size();
    for(auto id:n.inputs) {
       if(!nodes.contains(id))return Result<int>::err({Error::Code::InvalidArgument,"unknown input to node "+std::to_string(n.id)});
       if(!nodes.at(id)->produces_value)return Result<int>::err({Error::Code::InvalidArgument,"input does not produce a value"});
      out[id].push_back(n.id);
    }
    if(!n.required&&(!n.inputs.empty()||n.side_effect||n.access||n.call||n.terminator||n.may_trap||n.control!=schedrow::ControlFlow::None||!n.block_targets.empty()))return Result<int>::err({Error::Code::InvalidArgument,"external values must be effect-free leaves"});
  }
  for(auto& d:p.dependencies) {
    if(!nodes.contains(d.producer)||!nodes.contains(d.consumer))return Result<int>::err({Error::Code::InvalidArgument,"unknown semantic dependency endpoint"});
    if(d.distance)return Result<int>::err({Error::Code::Unsupported,"loop-carried selection dependencies require a CFG adapter"});
    ++indegree[d.consumer];out[d.producer].push_back(d.consumer);
  }
  std::set<NodeId> ready;for(auto [id,count]:indegree)if(!count)ready.insert(id);
  size_t visited=0;
  while(!ready.empty()){auto id=*ready.begin();ready.erase(ready.begin());++visited;for(auto child:out[id])if(--indegree[child]==0)ready.insert(child);}
  if(visited!=nodes.size())return Result<int>::err({Error::Code::Conflict,"cyclic source dependencies"});
  auto cfg=validate_cfg(p);if(!cfg)return cfg;
   for(auto id:p.outputs)if(!nodes.contains(id)||!nodes.at(id)->produces_value)return Result<int>::err({Error::Code::InvalidArgument,"unknown or non-value program output"});
  std::unordered_set<PatternId> ids;
  std::function<bool(const PatternTree&)> valid_tree=[&](const PatternTree& tree){
    if(tree.immediate&&tree.immediate->first>tree.immediate->second)return false;
    if(tree.op.empty())return tree.inputs.empty();
    return std::all_of(tree.inputs.begin(),tree.inputs.end(),valid_tree);
  };
  for(auto& pat:patterns) {
    if(!ids.insert(pat.id).second||pat.cost<0||pat.instruction.empty()||pat.root_op.empty())return Result<int>::err({Error::Code::InvalidArgument,"invalid instruction pattern"});
    if(pat.tree&&(!valid_tree(*pat.tree)||pat.tree->op!=pat.root_op))return Result<int>::err({Error::Code::InvalidArgument,"invalid structured pattern"});
    std::vector<std::string> bindings;auto tree=pattern_tree(pat);std::vector<const PatternTree*> work{&tree};
    while(!work.empty()){auto node=work.back();work.pop_back();if(!node->binding.empty())bindings.push_back(node->binding);for(auto& child:node->inputs)work.push_back(&child);}
    auto legal=metacode::validate_operand_constraints(pat.constraints,bindings);if(!legal)return Result<int>::err({legal.error().code,(pat.origin.empty()?"":pat.origin+": ")+"pattern "+pat.name+": "+legal.error().message});
    legal=metacode::validate_host_constraints(pat.host_constraints);if(!legal)return legal;
    if(pat.fused_memory){if(!pat.supports_side_effects||pat.host_constraints.empty())return Result<int>::err({Error::Code::InvalidArgument,"memory fusion requires effect support and a named proof"});auto memory=schedrow::metadata::validate_memory_access(*pat.fused_memory);if(!memory)return memory;}
  }
  return Result<int>::ok(0);
}
Result<std::vector<Candidate>> match_checked(const Program& input,const std::vector<Pattern>& patterns) {
  using Output=Result<std::vector<Candidate>>;
  auto prepared=prepare(input);if(!prepared)return Output::err(prepared.error());const auto& p=prepared.value();
  auto valid=validate(p,patterns);if(!valid)return Output::err(valid.error());
  for(auto& pattern:patterns)for(auto& predicate:pattern.host_constraints)if(!predicate.prove)return Output::err({Error::Code::Unsupported,pattern.origin+": unbound target predicate: "+predicate.name});
  std::map<NodeId,const Node*> nodes;std::map<NodeId,std::vector<NodeId>> users;
  for(auto& n:p.nodes){nodes[n.id]=&n;for(auto id:n.inputs)users[id].push_back(n.id);}
  std::vector<Candidate> candidates;
  for(auto& n:p.nodes)if(n.required)for(auto& pat:patterns)if(n.op==pat.root_op) {
     Candidate c{pat.id,n.id,{},pat.cost,pat.name,{}, n.produces_value?std::vector<NodeId>{n.id}:std::vector<NodeId>{}};
    std::map<std::string,NodeId> bindings;bool effects=false;
    std::function<bool(NodeId,const PatternTree&)> visit=[&](NodeId id,const PatternTree& tree) {
       const auto& node=*nodes.at(id);
       if(!tree.op.empty()&&node.block!=n.block)return false;
       if(!tree.type.empty()&&tree.type!=node.type)return false;
       if(!tree.register_class.empty()&&tree.register_class!=node.register_class)return false;
      if(tree.immediate&&(!node.constant||*node.constant<tree.immediate->first||*node.constant>tree.immediate->second))return false;
      if(!tree.binding.empty()) {
        auto [it,added]=bindings.emplace(tree.binding,id);if(!added&&it->second!=id)return false;
      }
       if(tree.op.empty()){if(!node.produces_value)return false;c.inputs.push_back(id);return true;}
      if(tree.op!=node.op||tree.inputs.size()!=node.inputs.size()||!node.required)return false;
      if(std::find(c.covered.begin(),c.covered.end(),id)!=c.covered.end())return false;
        c.covered.push_back(id);effects|=node.side_effect||bool(node.access)||node.call||node.terminator||node.may_trap||node.control!=schedrow::ControlFlow::None;
      for(size_t i=0;i<node.inputs.size();++i)if(!visit(node.inputs[i],tree.inputs[i]))return false;
      return true;
    };
    auto tree=pattern_tree(pat);
    if(!visit(n.id,tree)||(effects&&!pat.supports_side_effects))continue;
    auto memory_count=std::count_if(c.covered.begin(),c.covered.end(),[&](auto id){return bool(nodes.at(id)->access);});
    if((memory_count>1&&!pat.fused_memory)||(pat.fused_memory&&!memory_count))continue;
    if(!std::all_of(pat.constraints.begin(),pat.constraints.end(),[&](auto& constraint){auto& operand=*nodes.at(bindings.at(constraint.operand));std::optional<metacode::BoundOperand> other;if(metacode::relational(constraint.predicate)){auto& node=*nodes.at(bindings.at(constraint.other));other=metacode::BoundOperand{node.id,node.constant};}return metacode::satisfies(constraint,{operand.id,operand.constant},other);}))continue;
    if(pat.host_constraints.empty()&&std::any_of(c.covered.begin(),c.covered.end(),[&](auto id){return !nodes.at(id)->metadata.empty();}))continue;
    if(!pat.host_constraints.empty()){
      auto facts=[&](NodeId id){auto& node=*nodes.at(id);return schedrow::operand_facts(id,node.op,node.type,node.register_class,node.constant,node.metadata,node.access,node.side_effect,node.call,node.terminator,node.may_trap,node.inputs);};
      metacode::Value::Object bound;for(auto& [name,id]:bindings)bound[name]=facts(id);metacode::Value::Array covered;for(auto id:c.covered)covered.push_back(facts(id));
      metacode::Value::Object context{{"root",facts(n.id)},{"bindings",metacode::Value(std::move(bound))},{"covered",metacode::Value(std::move(covered))}};
      if(pat.fused_memory){context["memory_contract"]=schedrow::metadata::value(*pat.fused_memory);metacode::Value::Array order;for(auto& node:p.nodes)if(std::find(c.covered.begin(),c.covered.end(),node.id)!=c.covered.end())order.push_back(facts(node.id));context["source_order"]=metacode::Value(std::move(order));}
      auto proved=metacode::prove_host_constraints(pat.host_constraints,context);if(!proved)return Output::err(proved.error());if(!proved.value())continue;
      for(auto& predicate:pat.host_constraints)c.reason+="; proved "+predicate.name;
    }
    for(auto& constraint:pat.constraints){c.reason+="; proved "+std::string(metacode::predicate_name(constraint.predicate))+"("+constraint.operand;if(!constraint.other.empty())c.reason+=","+constraint.other;if(constraint.value)c.reason+=","+std::to_string(*constraint.value);c.reason+=")";}
     std::sort(c.covered.begin(),c.covered.end());
     if(std::any_of(c.inputs.begin(),c.inputs.end(),[&](auto id){return std::binary_search(c.covered.begin(),c.covered.end(),id);}))continue;
    bool escapes=false;
    for(auto id:c.covered)if(id!=n.id) {
      for(auto user:users[id])if(!std::binary_search(c.covered.begin(),c.covered.end(),user))escapes=true;
      if(std::find(p.outputs.begin(),p.outputs.end(),id)!=p.outputs.end())escapes=true;
      for(auto& b:p.blocks)if(std::find(b.live_out.begin(),b.live_out.end(),id)!=b.live_out.end())escapes=true;
      if(nodes.at(id)->control!=schedrow::ControlFlow::None)escapes=true;
      // Preserve effect placement: explicit ordering may not be hidden by fusion.
      for(auto& d:p.dependencies)if(d.producer==id||d.consumer==id)if(!pat.fused_memory||!std::binary_search(c.covered.begin(),c.covered.end(),d.producer)||!std::binary_search(c.covered.begin(),c.covered.end(),d.consumer))escapes=true;
    }
    if(escapes)continue;
    candidates.push_back(std::move(c));
  }
  std::sort(candidates.begin(),candidates.end(),[](auto& a,auto& b){return std::tie(a.root,a.cost,a.pattern)<std::tie(b.root,b.cost,b.pattern);});
  return Output::ok(std::move(candidates));
}
std::vector<Candidate> match(const Program& input,const std::vector<Pattern>& patterns) {
  auto result=match_checked(input,patterns);return result?std::move(result.value()):std::vector<Candidate>{};
}
Result<ConstraintModel> build_model(const Program& input,const std::vector<Pattern>& patterns) {
  auto prepared=prepare(input);if(!prepared)return Result<ConstraintModel>::err(prepared.error());const auto& p=prepared.value();
  auto valid=validate(p,patterns);if(!valid)return Result<ConstraintModel>::err(valid.error());
  auto matched=match_checked(p,patterns);if(!matched)return Result<ConstraintModel>::err(matched.error());ConstraintModel m{std::move(matched.value()),{}};
  if(m.candidates.size()>static_cast<size_t>(std::numeric_limits<int32_t>::max()/128))return Result<ConstraintModel>::err({Error::Code::ResourceLimit,"too many candidates"});
  for(auto& n:p.nodes)if(n.required) {
    std::vector<int32_t> covering;
    for(size_t i=0;i<m.candidates.size();++i)if(std::binary_search(m.candidates[i].covered.begin(),m.candidates[i].covered.end(),n.id))covering.push_back(static_cast<int32_t>(i+1));
    // Exactly one implementation prevents both missing coverage and overlap.
    m.clauses.push_back(covering);
    for(size_t i=0;i<covering.size();++i)for(size_t j=i+1;j<covering.size();++j)m.clauses.push_back({-covering[i],-covering[j]});
  }
  return Result<ConstraintModel>::ok(std::move(m));
}
namespace {
// Selection variables are 1-based Satie variables in candidate order, so the
// solver adapter below never exposes vendor types through unisel.hpp.
bool has_empty_clause(const std::vector<std::vector<int32_t>>& clauses) {
  return std::any_of(clauses.begin(),clauses.end(),[](auto& c){return c.empty();});
}
bool satisfiable(const std::vector<std::vector<int32_t>>& clauses) {
  // CDCL cannot ingest an empty clause (nothing to watch); it is UNSAT by
  // definition, e.g. a required operation with no covering candidate.
  if(has_empty_clause(clauses))return false;
  satie::CNF cnf;
  for(auto& clause:clauses)cnf.add_clause(satie::Clause(clause.begin(),clause.end()));
  return satie::solve_cdcl(cnf).status==satie::SolveStatus::SAT;
}
// Pseudo-Boolean cost bound over the selection variables, bit-blasted with
// Satie's theory encoder: sum(cost[i] * select[i]) <= bound. Hard coverage
// clauses stay native CNF; only the weighted bound uses integer reasoning.
std::vector<std::vector<int32_t>> bounded(const std::vector<std::vector<int32_t>>& hard,const std::vector<Candidate>& candidates,uint64_t bound) {
  const size_t n=candidates.size();
  satie::frontend::PBConstraint limit;
  for(size_t i=0;i<n;++i)if(candidates[i].cost>0)limit.terms.push_back({candidates[i].cost,static_cast<int>(i+1)});
  limit.cmp=satie::frontend::PBCmp::Le;
  limit.rhs=static_cast<long long>(bound);
  if(limit.terms.empty())return hard;
  satie::frontend::PBProblem probe;
  probe.variables=static_cast<int>(n);
  probe.constraints.push_back(limit);
  const int width=satie::frontend::pb_working_width(probe);
  satie::theory::Encoder enc(static_cast<satie::Var>(n+1));
  for(auto& clause:hard)enc.add_clause(satie::Clause(clause.begin(),clause.end()));
  std::vector<satie::Lit> bits;bits.reserve(n);
  for(size_t v=1;v<=n;++v)bits.push_back(static_cast<satie::Lit>(v));
  satie::frontend::blast_pb_constraint(enc,bits,limit,width);
  std::vector<std::vector<int32_t>> out;
  for(auto& clause:enc.take_clauses())out.emplace_back(clause.begin(),clause.end());
  return out;
}
}
Result<Selection> solve(const Program& p,const std::vector<Pattern>& patterns) {
  auto model=build_model(p,patterns);if(!model)return Result<Selection>::err(model.error());
  auto& m=model.value();uint64_t total=0;for(auto& c:m.candidates)total+=static_cast<uint32_t>(c.cost);
  if(!satisfiable(m.clauses))return Result<Selection>::err({Error::Code::Unsatisfiable,"no exact instruction covering satisfies the selection model"});
  uint64_t low=0,high=total;
  while(low<high){auto mid=low+(high-low)/2;if(satisfiable(bounded(m.clauses,m.candidates,mid)))high=mid;else low=mid+1;}
  if(low>std::numeric_limits<int>::max())return Result<Selection>::err({Error::Code::ResourceLimit,"selection cost overflow"});
  auto cnf=bounded(m.clauses,m.candidates,low);Selection s;
  // Fix the optimum lexicographically; vendor decision order is not observable.
  for(size_t i=0;i<m.candidates.size();++i) {
    auto lit=static_cast<int32_t>(i+1);cnf.push_back({lit});
    if(satisfiable(cnf)){s.selected.push_back(m.candidates[i]);s.cost+=m.candidates[i].cost;}
    else cnf.back()[0]=-lit;
  }
  auto emitted=emit_scheduler(p,patterns,s);if(!emitted)return Result<Selection>::err(emitted.error());
  return Result<Selection>::ok(std::move(s));
}
Result<Selection> solve_greedy(const Program& input,const std::vector<Pattern>& patterns) {
  auto prepared=prepare(input);if(!prepared)return Result<Selection>::err(prepared.error());const auto& p=prepared.value();
  auto valid=validate(p,patterns);if(!valid)return Result<Selection>::err(valid.error());
  auto matched=match_checked(p,patterns);if(!matched)return Result<Selection>::err(matched.error());auto candidates=std::move(matched.value());std::sort(candidates.begin(),candidates.end(),[](auto& a,auto& b){return std::tie(a.cost,a.pattern,a.root)<std::tie(b.cost,b.pattern,b.root);});
  Selection s;std::unordered_set<NodeId> covered;int64_t cost=0;
  for(auto& c:candidates)if(std::none_of(c.covered.begin(),c.covered.end(),[&](auto id){return covered.contains(id);})) {
    s.selected.push_back(c);cost+=c.cost;covered.insert(c.covered.begin(),c.covered.end());
  }
  if(covered.size()!=static_cast<size_t>(std::count_if(p.nodes.begin(),p.nodes.end(),[](auto& n){return n.required;})))return Result<Selection>::err({Error::Code::Unsatisfiable,"greedy selection cannot cover all required operations"});
  if(cost>std::numeric_limits<int>::max())return Result<Selection>::err({Error::Code::ResourceLimit,"selection cost overflow"});
  s.cost=static_cast<int>(cost);auto emitted=emit_scheduler(p,patterns,s);if(!emitted)return Result<Selection>::err(emitted.error());
  return Result<Selection>::ok(std::move(s));
}
Result<schedrow::Region> emit_scheduler(const Program& input,const std::vector<Pattern>& patterns,const Selection& selection) {
  auto prepared=prepare(input);if(!prepared)return Result<schedrow::Region>::err(prepared.error());const auto& p=prepared.value();
  auto valid=validate(p,patterns);if(!valid)return Result<schedrow::Region>::err(valid.error());
  auto matched=match_checked(p,patterns);if(!matched)return Result<schedrow::Region>::err(matched.error());auto candidates=std::move(matched.value());std::map<NodeId,NodeId> owner;std::map<PatternId,const Pattern*> pats;
  for(auto& pat:patterns)pats[pat.id]=&pat;
  int64_t cost=0;
  for(auto& c:selection.selected) {
    auto found=std::find_if(candidates.begin(),candidates.end(),[&](auto& x){return x.root==c.root&&x.pattern==c.pattern&&x.covered==c.covered&&x.inputs==c.inputs&&x.outputs==c.outputs&&x.cost==c.cost;});
    if(found==candidates.end())return Result<schedrow::Region>::err({Error::Code::InvalidArgument,"selection contains an invalid candidate"});
    for(auto id:c.covered)if(!owner.emplace(id,c.root).second)return Result<schedrow::Region>::err({Error::Code::Conflict,"overlapping selected candidates"});
    cost+=c.cost;
  }
  for(auto& n:p.nodes)if(n.required&&!owner.contains(n.id))return Result<schedrow::Region>::err({Error::Code::Conflict,"uncovered source operation"});
  if(cost!=selection.cost)return Result<schedrow::Region>::err({Error::Code::Conflict,"selection cost mismatch"});
  schedrow::Region r{"unisel"};r.blocks=p.blocks;r.entry=p.entry;std::set<std::tuple<NodeId,NodeId,schedrow::DepKind,uint32_t,bool>> edges;
  std::map<NodeId,const Node*> nodes;for(auto& n:p.nodes)nodes[n.id]=&n;
  std::map<NodeId,size_t> position;for(size_t k=0;k<p.nodes.size();++k)position[p.nodes[k].id]=k;
  auto selected=selection.selected;
  std::sort(selected.begin(),selected.end(),[](auto& a,auto& b){return a.root<b.root;});
  for(auto& c:selected) {
    schedrow::Instruction i{};i.id=c.root;i.opcode=pats.at(c.pattern)->instruction;i.defs=c.outputs;i.uses=c.inputs;
    i.access=pats.at(c.pattern)->fused_memory;i.memory=bool(i.access);
    i.block=nodes.at(c.root)->block;i.block_targets=nodes.at(c.root)->block_targets;i.control=nodes.at(c.root)->control;
    for(auto value:c.inputs)if(!nodes.at(value)->register_class.empty())i.register_classes[value]=nodes.at(value)->register_class;
    for(auto value:c.outputs)if(!nodes.at(value)->register_class.empty())i.register_classes[value]=nodes.at(value)->register_class;
    for(auto id:c.inputs)if(!nodes.at(id)->metadata.empty())i.source_metadata[id]=nodes.at(id)->metadata;
      i.origin="pattern "+std::to_string(c.pattern)+", root "+std::to_string(c.root);
      if(!pats.at(c.pattern)->origin.empty())i.origin+="; "+pats.at(c.pattern)->origin;
     for(auto id:c.covered) {
       const auto& node=*std::find_if(p.nodes.begin(),p.nodes.end(),[&](auto& n){return n.id==id;});
       if(!node.metadata.empty())i.source_metadata[id]=node.metadata;
       if(node.constant)i.immediates.emplace_back(id,*node.constant);
         if(node.side_effect||node.access||node.call||node.terminator||node.may_trap||node.control!=schedrow::ControlFlow::None)i.speculative=false;
        if(node.access) {
          if(!pats.at(c.pattern)->fused_memory){if(i.access)return Result<schedrow::Region>::err({Error::Code::Unsupported,"fusing multiple memory accesses requires a target effect adapter"});i.access=node.access;}i.memory=true;
        }
          i.call|=node.call;i.terminator|=node.terminator;i.may_trap|=node.may_trap;
          i.call|=node.control==schedrow::ControlFlow::Call;i.terminator|=node.control!=schedrow::ControlFlow::None&&node.control!=schedrow::ControlFlow::Call;
         i.barrier|=node.side_effect&&!node.access&&!node.call&&!node.terminator;
        if(!node.origin.empty())i.origin+="; "+node.origin;
     }
    r.instructions.push_back(std::move(i));
    for(auto input:c.inputs)if(owner.contains(input)&&owner.at(input)!=c.root)edges.emplace(owner.at(input),c.root,schedrow::DepKind::True,0,false);
  }
  for(auto& d:p.dependencies) {
    if(!owner.contains(d.producer)||!owner.contains(d.consumer))return Result<schedrow::Region>::err({Error::Code::Unsupported,"semantic dependency involves an external value"});
    if(owner.at(d.producer)!=owner.at(d.consumer))edges.emplace(owner.at(d.producer),owner.at(d.consumer),d.kind,d.latency,d.scheduler_only);
  }
  for(auto [a,b,kind,latency,scheduling]:edges)r.deps.push_back({a,b,kind,latency,0,scheduling});
  // Detect dependency cycles before exposing a selection to a scheduler.
  std::map<NodeId,size_t> indegree;std::map<NodeId,std::vector<NodeId>> out;
  for(auto& i:r.instructions)indegree[i.id]=0;
  for(auto& d:r.deps){++indegree[d.consumer];out[d.producer].push_back(d.consumer);}
  std::set<NodeId> ready;for(auto [id,n]:indegree)if(!n)ready.insert(id);
  std::vector<schedrow::Instruction> ordered;
  while(!ready.empty()){
    auto chosen=std::min_element(ready.begin(),ready.end(),[&](auto a,auto b){return std::tie(position[a],a)<std::tie(position[b],b);});auto id=*chosen;ready.erase(chosen);
    ordered.push_back(*std::find_if(r.instructions.begin(),r.instructions.end(),[&](auto& i){return i.id==id;}));
    for(auto child:out[id])if(--indegree[child]==0)ready.insert(child);
  }
  if(ordered.size()!=r.instructions.size())return Result<schedrow::Region>::err({Error::Code::Conflict,"selected instruction dependencies are cyclic"});
  r.instructions=std::move(ordered);return Result<schedrow::Region>::ok(std::move(r));
}
}
