#include "unisel_adapter.hpp"
#include <limits>
#include <map>
#include <set>

namespace limestone::tunah {
Result<GraphOptimization> optimize_graph(const unisel::Program& input,const Session& session,const GraphAdapterOptions& options) {
  auto checked=unisel::validate(input,{});if(!checked)return Result<GraphOptimization>::err(checked.error());
  std::map<std::string,std::pair<std::string,GraphOperator>> reverse;
  for(auto& [op,signature]:options.operators) {
    if(op.empty()||signature.term_operator.empty()||signature.type.empty()||op==options.constant_opcode||!reverse.emplace(signature.term_operator,std::pair{op,signature}).second)return Result<GraphOptimization>::err({Error::Code::InvalidArgument,"ambiguous or untyped graph operator"});
    auto found=session.operators().find(signature.term_operator);
    if(found==session.operators().end()||found->second!=signature.arity)return Result<GraphOptimization>::err({Error::Code::InvalidArgument,"graph adapter and rule operator signatures disagree"});
  }
  // Canonical operand order must not make directional user rules unreachable.
  // Admit the explicitly declared commutative equivalence through Equinox too.
  auto engine=session;
  for(auto& [term_operator,entry]:reverse)if(entry.second.commutative) {
    if(entry.second.arity!=2)return Result<GraphOptimization>::err({Error::Code::InvalidArgument,"commutative graph operators must be binary"});
    Term left;left.op="?left";Term right;right.op="?right";
    Term lhs;lhs.op=term_operator;lhs.application=true;lhs.arguments={left,right};
    Term rhs=lhs;std::reverse(rhs.arguments.begin(),rhs.arguments.end());
    engine.add_rule({"adapter.commute."+term_operator,std::move(lhs),std::move(rhs),{}, {"<graph-adapter>"}});
  }
  GraphOptimization result;result.program=input;
  std::map<unisel::NodeId,unisel::Node> nodes;std::map<unisel::NodeId,size_t> position;std::set<unisel::NodeId> pinned;uint64_t next=0;
  for(size_t k=0;k<input.nodes.size();++k){auto& n=input.nodes[k];nodes[n.id]=n;position[n.id]=k;next=std::max(next,uint64_t(n.id)+1);result.values[n.id]=n.id;if(!n.metadata.empty())pinned.insert(n.id);}
  for(auto& d:input.dependencies){pinned.insert(d.producer);pinned.insert(d.consumer);}
  // Topological traversal is independent of caller vector and numeric ID order.
  std::map<unisel::NodeId,size_t> degree;std::map<unisel::NodeId,std::vector<unisel::NodeId>> users;
  for(auto& n:input.nodes){degree[n.id]=n.inputs.size();for(auto id:n.inputs)users[id].push_back(n.id);}
  std::set<unisel::NodeId> ready;for(auto [id,count]:degree)if(!count)ready.insert(id);std::vector<unisel::NodeId> order;
  while(!ready.empty()){auto id=*ready.begin();ready.erase(ready.begin());order.push_back(id);for(auto user:users[id])if(!--degree[user])ready.insert(user);}
  std::function<unisel::NodeId(unisel::NodeId)> resolve=[&](auto id){auto mapped=result.values.at(id);return mapped==id?id:result.values[id]=resolve(mapped);};
  size_t created=0;
  try {
    for(auto id:order) {
      auto original=nodes.at(id);for(auto& operand:original.inputs)operand=resolve(operand);nodes[id]=original;
      auto signature=options.operators.find(original.op);
      if(signature==options.operators.end()||pinned.contains(id)||!original.required||!original.produces_value||original.side_effect||original.access||original.call||original.terminator||original.may_trap||original.control!=schedrow::ControlFlow::None)continue;
      const auto& model=signature->second;
      if(original.type!=model.type||original.inputs.size()!=model.arity)throw Error{Error::Code::InvalidArgument,"typed graph operator mismatch at node "+std::to_string(id)};
      Term root;root.op=model.term_operator;root.application=true;
      std::map<std::string,unisel::NodeId> boundaries;
      for(auto operand:original.inputs) {
        const auto& n=nodes.at(operand);if(n.type!=model.type)throw Error{Error::Code::InvalidArgument,"graph operand type mismatch at node "+std::to_string(id)};
        Term term;
        if(n.op==options.constant_opcode&&n.constant&&n.produces_value&&n.required&&n.block==original.block&&!n.side_effect&&!n.access&&!n.call&&!n.terminator&&!n.may_trap&&n.control==schedrow::ControlFlow::None&&!pinned.contains(operand))term.constant=n.constant;
        else {term.op="value_"+std::to_string(operand);boundaries[term.op]=operand;}
        root.arguments.push_back(std::move(term));
      }
      if(model.commutative)std::sort(root.arguments.begin(),root.arguments.end(),[](auto& a,auto& b){return format_term(a)<format_term(b);});
      auto optimized=engine.saturate(root,options.limits,options.costs);if(!optimized)return Result<GraphOptimization>::err(optimized.error());
      result.rewrites+=optimized.value().rewrites;result.limit_reached|=optimized.value().limit_reached;
      // Validate the entire extracted tree before modifying this node's graph.
      std::function<void(const Term&,size_t)> legal=[&](auto& term,size_t depth) {
        if(depth>256)throw Error{Error::Code::ResourceLimit,"graph reconstruction nesting limit"};
        if(term.constant){if(!term.arguments.empty())throw Error{Error::Code::Conflict,"constant has operands"};return;}
        if(!term.application&&term.arguments.empty()) {if(!boundaries.contains(term.op))throw Error{Error::Code::Conflict,"extraction introduced an unknown graph value"};return;}
        auto s=reverse.find(term.op);if(s==reverse.end()||s->second.second.type!=model.type||s->second.second.arity!=term.arguments.size())throw Error{Error::Code::Conflict,"extraction introduced an illegal typed operator"};
        for(auto& argument:term.arguments)legal(argument,depth+1);
      };legal(optimized.value().term,0);
      std::map<std::string,unisel::NodeId> interned;
      std::function<unisel::NodeId(const Term&,bool)> reconstruct=[&](auto& term,bool is_root)->unisel::NodeId {
        if(!term.constant&&!term.application&&term.arguments.empty())return boundaries.at(term.op);
        auto key=format_term(term);if(!is_root&&interned.contains(key))return interned.at(key);
        uint32_t destination=id;
        if(!is_root){if(++created>options.reconstructed_nodes||next>std::numeric_limits<uint32_t>::max())throw Error{Error::Code::ResourceLimit,"graph reconstruction node limit"};destination=static_cast<uint32_t>(next++);result.values[destination]=destination;position[destination]=position[id];}
        unisel::Node node=original;node.id=destination;node.inputs.clear();node.constant.reset();
        if(term.constant){node.op=options.constant_opcode;node.constant=term.constant;}
        else {node.op=reverse.at(term.op).first;for(auto& arg:term.arguments)node.inputs.push_back(reconstruct(arg,false));}
        nodes[destination]=std::move(node);interned[key]=destination;return destination;
      };
      auto replacement=reconstruct(optimized.value().term,true);result.values[id]=replacement;
      if(replacement!=id)nodes.erase(id);
    }
    result.program.nodes.clear();
    // Preserve observable source order; generated pure nodes share their root's
    // source position and are ordered by dataflow during selection.
    for(auto& [id,n]:nodes){for(auto& operand:n.inputs)operand=resolve(operand);result.program.nodes.push_back(n);}
    std::stable_sort(result.program.nodes.begin(),result.program.nodes.end(),[&](auto& a,auto& b){return std::tie(position[a.id],a.id)<std::tie(position[b.id],b.id);});
    for(auto& output:result.program.outputs)output=resolve(output);
    for(auto& block:result.program.blocks)for(auto& output:block.live_out)output=resolve(output);
    std::set<unisel::NodeId> live;
    std::vector<unisel::NodeId> work=result.program.outputs;
    for(auto& block:result.program.blocks)work.insert(work.end(),block.live_out.begin(),block.live_out.end());
    for(auto& node:result.program.nodes) {
      bool pure=node.op==options.constant_opcode||options.operators.contains(node.op);
      if(!node.required||!pure||pinned.contains(node.id)||node.side_effect||node.access||node.call||node.terminator||node.may_trap||node.control!=schedrow::ControlFlow::None)work.push_back(node.id);
    }
    while(!work.empty()){auto id=work.back();work.pop_back();if(!live.insert(id).second)continue;for(auto operand:nodes.at(id).inputs)work.push_back(resolve(operand));}
    std::erase_if(result.program.nodes,[&](auto& node){return !live.contains(node.id);});
    for(auto& [id,mapped]:result.values)mapped=resolve(id);
    checked=unisel::validate(result.program,{});if(!checked)return Result<GraphOptimization>::err(checked.error());
    return Result<GraphOptimization>::ok(std::move(result));
  }catch(const Error& error){return Result<GraphOptimization>::err(error);}
}
}
