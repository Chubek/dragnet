#include "target.hpp"
#include <limits>
#include <map>
#include <set>

namespace limestone::limeburg {
Result<RuleSet> from_umd(const unisel::MachineDescription& machine) {
  auto valid=unisel::validate(machine);if(!valid)return Result<RuleSet>::err(valid.error());RuleSet rules;rules.nonterminals["value"]=0;
  for(auto& p:machine.patterns) {
    Rule rule{p.id,"value",p.root_op,"value",{},p.cost,p.instruction};rule.supports_side_effects=p.supports_side_effects;rule.origin=p.origin;
    rule.constraints=p.constraints;
    rule.host_constraints=p.host_constraints;
    rule.fused_memory=p.fused_memory;
    std::function<Pattern(const unisel::PatternTree&)> convert=[&](auto& t) {
      Pattern pattern;pattern.op=t.op;pattern.type=t.type;pattern.immediate=t.immediate;pattern.binding=t.binding;pattern.register_class=t.register_class;
      if(t.op.empty())pattern.nonterminal="value";
      for(auto& child:t.inputs)pattern.children.push_back(convert(child));return pattern;
    };
    rule.pattern=convert(unisel::pattern_tree(p));
    rules.rules.push_back(std::move(rule));
  }
  valid=validate(rules);if(!valid)return Result<RuleSet>::err(valid.error());return Result<RuleSet>::ok(std::move(rules));
}
Result<schedrow::Region> select_graph(const unisel::Program& source_program,const RuleSet& input,std::string_view root_nt,GraphPolicy policy) {
  auto prepared=unisel::prepare(source_program);if(!prepared)return Result<schedrow::Region>::err(prepared.error());const auto& program=prepared.value();
  auto valid=unisel::validate(program,{});if(!valid)return Result<schedrow::Region>::err(valid.error());
  std::map<uint32_t,const unisel::Node*> source;std::map<uint32_t,size_t> uses;
  auto rules=input;uint64_t next=0;
  for(auto& rule:rules.rules)next=std::max(next,uint64_t(rule.id)+1);
  for(auto& n:program.nodes) {
    source[n.id]=&n;for(auto child:n.inputs)++uses[child];
  }
  if(policy!=GraphPolicy::TreeOnly&&policy!=GraphPolicy::PreserveShared)return Result<schedrow::Region>::err({Error::Code::InvalidArgument,"unknown BURS graph policy"});
  if(policy==GraphPolicy::TreeOnly)for(auto& n:program.nodes)if(n.required&&uses[n.id]>1)return Result<schedrow::Region>::err({Error::Code::Unsupported,"shared computation requires an explicit DAG selection policy"});
  bool memory_fusion=std::any_of(rules.rules.begin(),rules.rules.end(),[](auto& rule){return rule.fused_memory.has_value();});
  std::map<uint32_t,size_t> positions;for(size_t k=0;k<program.nodes.size();++k)positions[program.nodes[k].id]=k;
  auto fusible_edge=[&](const schedrow::Dependency& edge){
    auto& a=*source.at(edge.producer);auto& b=*source.at(edge.consumer);
    if(!memory_fusion||!a.access||!b.access||a.block!=b.block||uses[a.id]!=1||std::find(b.inputs.begin(),b.inputs.end(),a.id)==b.inputs.end()||positions[a.id]>=positions[b.id])return false;
    for(size_t k=positions[a.id]+1;k<positions[b.id];++k){auto& node=program.nodes[k];if(node.side_effect||node.access||node.call||node.terminator||node.may_trap||node.control!=schedrow::ControlFlow::None)return false;}
    return true;
  };
  std::set<uint32_t> pinned(program.outputs.begin(),program.outputs.end());for(auto& edge:program.dependencies)if(!fusible_edge(edge)){pinned.insert(edge.producer);pinned.insert(edge.consumer);}
  for(auto& b:program.blocks)pinned.insert(b.live_out.begin(),b.live_out.end());
  if(policy==GraphPolicy::TreeOnly)for(auto id:pinned)if(source.at(id)->required&&uses[id])return Result<schedrow::Region>::err({Error::Code::Unsupported,"observable intermediate requires a forest boundary adapter"});
  std::set<uint32_t> boundaries=pinned;
  for(auto& n:program.nodes)if(n.control!=schedrow::ControlFlow::None||n.call||n.terminator)boundaries.insert(n.id);
  if(policy==GraphPolicy::PreserveShared){for(auto& n:program.nodes){if(n.required&&uses[n.id]>1)boundaries.insert(n.id);for(auto id:n.inputs)if(source.at(id)->required&&source.at(id)->block!=n.block)boundaries.insert(id);}}
  schedrow::Region region{"limeburg"};region.blocks=program.blocks;region.entry=program.entry;std::map<uint32_t,uint32_t> owners;std::set<uint32_t> visited;
  for(auto& [root,ptr]:source)if(ptr->required&&(!uses[root]||(policy==GraphPolicy::PreserveShared&&boundaries.contains(root)))) {
    const auto& n=*ptr;
    std::vector<uint32_t> work{n.id};std::vector<Node> nodes;std::set<uint32_t> local;auto local_rules=rules;
    while(!work.empty()) {
      auto id=work.back();work.pop_back();const auto& node=*source.at(id);
      if(!local.insert(id).second)continue;
      bool boundary=!node.required||(policy==GraphPolicy::PreserveShared&&id!=root&&boundaries.contains(id));
      if(boundary) {
        if(next>std::numeric_limits<uint32_t>::max())return Result<schedrow::Region>::err({Error::Code::ResourceLimit,"BURS rule identity overflow"});
        Rule rule{static_cast<uint32_t>(next++),std::string(root_nt),node.op,std::string(root_nt),{},0,""};rule.type=node.type;rule.external_only=true;local_rules.rules.push_back(std::move(rule));
         nodes.push_back({id,node.op,node.type,{},0,false,false,true});nodes.back().register_class=node.register_class;nodes.back().known_constant=node.constant;nodes.back().metadata=node.metadata;continue;
      }
      if(!visited.insert(id).second&&node.required)return Result<schedrow::Region>::err({Error::Code::Unsupported,"shared forest computation"});
      if(node.required&&node.block!=n.block)return Result<schedrow::Region>::err({Error::Code::Unsupported,"BURS forest crosses a basic block"});
        nodes.push_back({id,node.op,node.type,node.inputs,node.constant.value_or(0),bool(node.constant),node.required,node.produces_value,node.side_effect||node.control!=schedrow::ControlFlow::None,node.access,node.call,node.terminator||(!node.block_targets.empty()),node.may_trap,node.origin,node.register_class});
       nodes.back().metadata=node.metadata;
      work.insert(work.end(),node.inputs.begin(),node.inputs.end());
    }
    // Predicate facts retain source declaration order, not forest traversal order.
    std::sort(nodes.begin(),nodes.end(),[&](auto& a,auto& b){return positions.at(a.id)<positions.at(b.id);});
    auto selection=select(nodes,n.id,local_rules,root_nt);if(!selection)return Result<schedrow::Region>::err(selection.error());
    auto emitted=emit_scheduler(nodes,local_rules,selection.value());if(!emitted)return emitted;
    std::map<RuleId,const Rule*> local_rule_ids;for(auto& r:local_rules.rules)local_rule_ids[r.id]=&r;
    std::vector<std::pair<uint32_t,uint32_t>> ownership{{root,root}};
    while(!ownership.empty()){auto [id,parent]=ownership.back();ownership.pop_back();auto& d=selection.value().chosen.at(id);auto owner=local_rule_ids.at(d.rule)->instruction.empty()?parent:id;for(auto covered:d.covered)if(source.at(covered)->required)owners[covered]=owner;for(auto child:d.children)ownership.emplace_back(child,owner);}
    region.instructions.insert(region.instructions.end(),emitted.value().instructions.begin(),emitted.value().instructions.end());region.deps.insert(region.deps.end(),emitted.value().deps.begin(),emitted.value().deps.end());
  }
  for(auto& n:program.nodes)if(n.required&&!owners.contains(n.id))return Result<schedrow::Region>::err({Error::Code::Conflict,"uncovered BURS forest node"});
  for(auto& instruction:region.instructions){auto& node=*source.at(instruction.id);instruction.block=node.block;instruction.block_targets=node.block_targets;instruction.control=node.control;instruction.call|=node.control==schedrow::ControlFlow::Call;instruction.terminator|=node.control!=schedrow::ControlFlow::None&&node.control!=schedrow::ControlFlow::Call;for(auto value:instruction.uses)if(owners.contains(value)&&owners.at(value)!=instruction.id&&std::none_of(region.deps.begin(),region.deps.end(),[&](auto& d){return d.producer==owners.at(value)&&d.consumer==instruction.id&&d.kind==schedrow::DepKind::True;}))region.deps.push_back({owners.at(value),instruction.id,schedrow::DepKind::True});}
  for(auto& instruction:region.instructions)for(auto id:instruction.defs)if(!source.at(id)->register_class.empty())instruction.register_classes[id]=source.at(id)->register_class;
  for(auto& instruction:region.instructions)for(auto id:instruction.uses)if(!source.at(id)->register_class.empty())instruction.register_classes[id]=source.at(id)->register_class;
  for(auto& edge:program.dependencies) {
    if(!owners.contains(edge.producer)||!owners.contains(edge.consumer))return Result<schedrow::Region>::err({Error::Code::Unsupported,"dependency involves an external value"});
    auto d=edge;d.producer=owners.at(edge.producer);d.consumer=owners.at(edge.consumer);if(d.producer!=d.consumer)region.deps.push_back(d);
  }
  std::map<uint32_t,size_t> degree,position;std::map<uint32_t,std::vector<uint32_t>> users;
  for(size_t k=0;k<program.nodes.size();++k)position[program.nodes[k].id]=k;
  for(auto& instruction:region.instructions)degree[instruction.id]=0;
  for(auto& edge:region.deps){++degree[edge.consumer];users[edge.producer].push_back(edge.consumer);}
  std::set<uint32_t> ready;for(auto [id,count]:degree)if(!count)ready.insert(id);std::vector<schedrow::Instruction> ordered;
  while(!ready.empty()){auto chosen=std::min_element(ready.begin(),ready.end(),[&](auto a,auto b){return std::tie(position[a],a)<std::tie(position[b],b);});auto id=*chosen;ready.erase(chosen);ordered.push_back(*std::find_if(region.instructions.begin(),region.instructions.end(),[&](auto& i){return i.id==id;}));for(auto user:users[id])if(!--degree[user])ready.insert(user);}
  if(ordered.size()!=region.instructions.size())return Result<schedrow::Region>::err({Error::Code::Conflict,"cyclic BURS forest dependencies"});region.instructions=std::move(ordered);
  return Result<schedrow::Region>::ok(std::move(region));
}
}
