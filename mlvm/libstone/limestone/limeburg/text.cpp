#include "text.hpp"
#include "parsers/limeburg_ast.hpp"
#include "parsers/semantic.hpp"
#include "parsers/source_internal.hpp"
#include "schedrow/memory_metadata.hpp"
#include <set>

namespace limestone::limeburg {
namespace {
namespace ast=syntax::limeburg;
namespace sem=syntax::semantic;
using Object=metacode::Value::Object;
std::unique_ptr<ast::Document> expand(std::string_view text,std::string_view file,syntax::detail::IncludeContext& context) {
  syntax::detail::IncludeScope scope(context,text,file);
  auto parsed=ast::parse(text,file);if(!parsed)throw parsed.error();auto document=std::move(parsed.value());
  auto included=[&](const ast::Include& include) {
    try {auto source=context.resolve(file,sem::spelling(*include.path));return expand(source.text,source.name,context);}
    catch(const Error& e){sem::fail(include,"include "+include.path->value+": "+e.message,e.code);}
  };
  std::vector<std::unique_ptr<ast::Declaration>> declarations;
  for(auto& declaration:document->declarations) {
    if(auto p=std::get_if<std::unique_ptr<ast::Include>>(&declaration->value)) {
      auto nested=included(**p);for(auto& child:nested->declarations)declarations.push_back(std::move(child));continue;
    }
    if(auto p=std::get_if<std::unique_ptr<ast::RuleSet>>(&declaration->value)) {
      std::vector<std::unique_ptr<ast::RuleDeclaration>> rules;
      for(auto& rule:(*p)->declarations) {
        if(auto include=std::get_if<std::unique_ptr<ast::Include>>(&rule->value)) {
          auto nested=included(**include);
          for(auto& child:nested->declarations)std::visit([&](auto& node){
            using T=std::remove_cvref_t<decltype(*node)>;
            if constexpr(std::is_same_v<T,ast::Nonterminal>||std::is_same_v<T,ast::Terminal>||std::is_same_v<T,ast::Rule>) {
              auto entry=std::make_unique<ast::RuleDeclaration>();entry->source=child->source;entry->value=std::move(node);rules.push_back(std::move(entry));
            }else sem::fail(*node,"only rule declarations may be included inside a ruleset",Error::Code::Conflict);
          },child->value);
        }else rules.push_back(std::move(rule));
      }
      (*p)->declarations=std::move(rules);
    }
    declarations.push_back(std::move(declaration));
  }
  document->declarations=std::move(declarations);return document;
}
Pattern pattern(const ast::Pattern& source,const RuleDocument& document) {
  Pattern p;auto symbol=source.symbol->value;
  if(!source.arguments&&document.rules.nonterminals.contains(symbol))p.nonterminal=symbol;
  else {p.op=symbol;auto term=document.terminals.find(symbol);if(term==document.terminals.end())sem::fail(source,"unknown terminal: "+symbol,Error::Code::NotFound);if((source.arguments?source.arguments->children.size():0)!=term->second)sem::fail(source,"terminal arity mismatch: "+symbol);if(source.arguments)for(auto& c:source.arguments->children)p.children.push_back(pattern(*c,document));}
  for(auto& constraint:source.constraints)std::visit([&](const auto& ptr){
    using T=std::remove_cvref_t<decltype(*ptr)>;
    if constexpr(std::is_same_v<T,ast::TypeConstraint>){if(!p.type.empty())sem::fail(*ptr,"duplicate pattern type");p.type=ptr->name->value;}
    else if constexpr(std::is_same_v<T,ast::ImmediateConstraint>){if(p.immediate)sem::fail(*ptr,"duplicate immediate constraint");p.immediate=std::pair{sem::integer(ptr->lower->value),sem::integer(ptr->upper->value)};}
    else if constexpr(std::is_same_v<T,ast::BindingConstraint>){if(!p.binding.empty())sem::fail(*ptr,"duplicate pattern binding");p.binding=ptr->name->value;}
    else {if(!p.register_class.empty())sem::fail(*ptr,"duplicate pattern register class");p.register_class=ptr->name->value;}
  },constraint->value);
  return p;
}
std::string print_pattern(const Pattern& p) {
  std::string out=p.nonterminal.empty()?p.op:p.nonterminal;
  if(p.nonterminal.empty()){out+='(';for(auto& c:p.children){if(out.back()!='(')out+=", ";out+=print_pattern(c);}out+=')';}
  if(!p.type.empty())out+=':'+p.type;
  if(p.immediate)out+='['+std::to_string(p.immediate->first)+".."+std::to_string(p.immediate->second)+']';if(!p.binding.empty())out+=" binding "+p.binding;if(!p.register_class.empty())out+=" register_class "+p.register_class;return out;
}
}
Result<RuleDocument> load_rules(std::string_view text,std::string_view file) {
  return load_rules(text,file,{});
}
Result<RuleDocument> load_rules(std::string_view text,std::string_view file,const syntax::IncludeOptions& options) {
  try {
    syntax::detail::IncludeContext context(options);auto parsed=expand(text,file,context);
    RuleDocument document;std::vector<const ast::Rule*> rules;std::vector<const ast::Tree*> trees;std::vector<const ast::Nonterminal*> nts;std::set<uint32_t> nt_ids,rule_ids;
    auto declaration=[&](const auto& node){std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;
      if constexpr(std::is_same_v<T,ast::Nonterminal>)nts.push_back(p.get());
      else if constexpr(std::is_same_v<T,ast::Terminal>){if(!document.terminals.emplace(p->name->value,sem::number(p->arity->value)).second)sem::fail(*p,"duplicate terminal",Error::Code::Conflict);}
      else if constexpr(std::is_same_v<T,ast::Rule>)rules.push_back(p.get());
      else if constexpr(std::is_same_v<T,ast::Tree>)trees.push_back(p.get());
      else if constexpr(std::is_same_v<T,ast::Include>)sem::fail(*p,"unexpanded include",Error::Code::Internal);
    },node.value);};
    for(auto& d:parsed->declarations)std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;if constexpr(std::is_same_v<T,ast::RuleSet>){if(!document.name.empty())sem::fail(*p,"multiple rulesets need separate documents",Error::Code::Conflict);document.name=sem::spelling(*p->name);for(auto& n:p->declarations)declaration(*n);}else declaration(*d);},d->value);
    if(document.name.empty())document.name="rules";
    for(auto n:nts)if(n->number){auto id=sem::number(n->number->value);if(!nt_ids.insert(id).second)sem::fail(*n,"duplicate nonterminal identity",Error::Code::Conflict);}
    uint64_t next=0;for(auto n:nts){uint32_t id;if(n->number)id=sem::number(n->number->value);else {while(nt_ids.contains(uint32_t(next))&&next<=UINT32_MAX)++next;if(next>UINT32_MAX)sem::fail(*n,"nonterminal identity overflow",Error::Code::ResourceLimit);id=uint32_t(next++);nt_ids.insert(id);}if(!document.rules.nonterminals.emplace(n->name->value,id).second||document.terminals.contains(n->name->value))sem::fail(*n,"duplicate terminal/nonterminal name",Error::Code::Conflict);}
    for(auto r:rules)if(r->number&&!rule_ids.insert(sem::number(r->number->value)).second)sem::fail(*r,"duplicate rule identity",Error::Code::Conflict);
    next=0;for(auto r:rules){Rule rule{};if(r->number)rule.id=sem::number(r->number->value);else {while(next<=UINT32_MAX&&rule_ids.contains(uint32_t(next)))++next;if(next>UINT32_MAX)sem::fail(*r,"rule identity overflow",Error::Code::ResourceLimit);rule.id=uint32_t(next++);rule_ids.insert(rule.id);}rule.lhs=rule.result=r->result->value;rule.pattern=pattern(*r->pattern,document);rule.op=rule.pattern->op;rule.instruction=sem::spelling(*r->instruction);rule.origin=r->source.file+":"+std::to_string(r->source.begin.line)+":"+std::to_string(r->source.begin.column);std::set<std::string> options;
      for(auto& o:r->options)std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;std::string key;
        if constexpr(std::is_same_v<T,ast::Cost>){key="cost";rule.cost=sem::small_integer(p->amount->value);}
        else if constexpr(std::is_same_v<T,ast::Priority>){key="priority";rule.priority=sem::small_integer(p->amount->value);}
        else if constexpr(std::is_same_v<T,ast::Type>){key="type";rule.type=p->name->value;}
        else if constexpr(std::is_same_v<T,ast::Immediate>){key="immediate";rule.immediate=std::pair{sem::integer(p->lower->value),sem::integer(p->upper->value)};}
        else if constexpr(std::is_same_v<T,ast::SideEffects>){key="side_effects";rule.supports_side_effects=sem::boolean(p->enabled->value);}
        else if constexpr(std::is_same_v<T,ast::Origin>){key="origin";rule.origin=sem::spelling(*p->name);}
        else if constexpr(std::is_same_v<T,ast::Where>){key="where";auto fields=sem::attributes(p->constraints->attributes);auto constraints=metacode::load_operand_constraints(fields);if(!constraints)sem::fail(*p,constraints.error().message,constraints.error().code);rule.constraints=std::move(constraints.value());auto host=metacode::load_host_constraints(fields);if(!host)sem::fail(*p,host.error().message,host.error().code);rule.host_constraints=std::move(host.value());}
        else if constexpr(std::is_same_v<T,ast::MemoryContract>){key="memory_contract";auto memory=schedrow::metadata::load_memory_access(sem::metadata(*p->access));if(!memory)sem::fail(*p,memory.error().message,memory.error().code);rule.fused_memory=std::move(memory.value());}
        else {key="external_only";rule.external_only=sem::boolean(p->enabled->value);}
        if(!options.insert(key).second)sem::fail(*p,"duplicate rule option: "+key,Error::Code::Conflict);
      },o->value);document.rules.rules.push_back(std::move(rule));
    }
    auto valid=validate(document.rules);if(!valid)return Result<RuleDocument>::err(valid.error());std::set<std::string> tree_names;
    for(auto t:trees){InputTree tree;tree.name=sem::spelling(*t->name);if(!tree_names.insert(tree.name).second)sem::fail(*t,"duplicate input tree",Error::Code::Conflict);std::set<std::string> names;for(auto& n:t->nodes)if(!names.insert(n->result->value).second)sem::fail(*n,"duplicate tree node",Error::Code::Conflict);sem::Names ids;ids.assign(names);tree.root=ids.at(t->root_node->reference->value);tree.nonterminal=t->root_node->nonterminal->value;if(!document.rules.nonterminals.contains(tree.nonterminal))sem::fail(*t,"unknown root nonterminal",Error::Code::NotFound);
      for(auto& n:t->nodes){Node node{};node.id=ids.at(n->result->value);node.op=n->opcode->value;auto term=document.terminals.find(node.op);if(term==document.terminals.end())sem::fail(*n,"unknown tree terminal",Error::Code::NotFound);if(term->second!=n->operands.size())sem::fail(*n,"tree terminal arity mismatch");for(auto& v:n->operands)node.children.push_back(ids.at(v->value));if(n->type)node.type=n->type->name->value;if(n->immediate){node.has_imm=true;node.imm=sem::integer(n->immediate->value);}node.origin=n->source.file+":"+std::to_string(n->source.begin.line);std::set<std::string> properties;
         for(auto& property:n->properties)std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;std::string key;if constexpr(std::is_same_v<T,ast::NodeClass>){key="register_class";node.register_class=p->name->value;}else if constexpr(std::is_same_v<T,ast::Origin>){key="origin";node.origin=sem::spelling(*p->name);}else if constexpr(std::is_same_v<T,ast::KnownConstant>){key="known_constant";node.known_constant=sem::integer(p->amount->value);}else if constexpr(std::is_same_v<T,ast::Required>){key="required";node.required=sem::boolean(p->enabled->value);}else if constexpr(std::is_same_v<T,ast::ProducesValue>){key="produces_value";node.produces_value=sem::boolean(p->enabled->value);}else if constexpr(std::is_same_v<T,ast::SideEffect>){key="side_effect";node.side_effect=sem::boolean(p->enabled->value);}else if constexpr(std::is_same_v<T,ast::MayTrap>){key="may_trap";node.may_trap=sem::boolean(p->enabled->value);}else if constexpr(std::is_same_v<T,ast::Call>){key="call";node.call=sem::boolean(p->enabled->value);}else if constexpr(std::is_same_v<T,ast::Properties>){key="properties";auto fields=sem::attributes(p->fields->attributes);if(fields.contains("access")){auto memory=schedrow::metadata::load_memory_access(fields.at("access"));if(!memory)sem::fail(*p,memory.error().message,memory.error().code);node.access=std::move(memory.value());}metacode::Value::Object payload{{"strings",metacode::Value(metacode::Value::Array{})},{"properties",metacode::Value(metacode::Value::Object{})}};if(fields.contains("strings"))payload["strings"]=fields.at("strings");if(fields.contains("metadata"))payload["properties"]=fields.at("metadata");auto metadata=metacode::load_operand_metadata(metacode::Value(std::move(payload)));if(!metadata)sem::fail(*p,metadata.error().message,metadata.error().code);node.metadata=std::move(metadata.value());for(auto& [name,value]:fields)if(name!="access"&&name!="strings"&&name!="metadata")sem::fail(*p,"unknown tree property: "+name,Error::Code::Unsupported);}else {key="terminator";node.terminator=sem::boolean(p->enabled->value);}if(!properties.insert(key).second)sem::fail(*p,"duplicate tree property",Error::Code::Conflict);},property->value);
         if(node.known_constant&&(node.required||node.has_imm))sem::fail(*n,"known constants belong to non-immediate external boundaries");
        if(!node.required&&(!node.children.empty()||node.side_effect||node.access||node.may_trap||node.call||node.terminator||!node.produces_value))sem::fail(*n,"external values must be effect-free leaves");for(auto& argument:node.metadata.strings)if(node.required&&argument.index>=node.children.size()+node.metadata.strings.size()+node.has_imm)sem::fail(*n,"invalid mixed string argument index");tree.nodes.push_back(std::move(node));}
      std::map<NodeId,const Node*> nodes;for(auto& n:tree.nodes)nodes[n.id]=&n;std::map<NodeId,int> state;std::vector<std::pair<NodeId,bool>> work{{tree.root,false}};
      while(!work.empty()){auto [id,done]=work.back();work.pop_back();if(done){state[id]=2;continue;}if(state[id]==1)sem::fail(*t,"cyclic input tree",Error::Code::Conflict);if(state[id]==2)continue;state[id]=1;work.emplace_back(id,true);for(auto child:nodes.at(id)->children){if(!nodes.at(child)->produces_value)sem::fail(*t,"tree operand does not produce a value");work.emplace_back(child,false);}}
      if(state.size()!=nodes.size())sem::fail(*t,"disconnected node outside the input tree",Error::Code::Conflict);document.trees.push_back(std::move(tree));
    }
    return Result<RuleDocument>::ok(std::move(document));
  }catch(const Error& e){return Result<RuleDocument>::err(e);}
}
Result<RuleDocument> load_rules_file(const std::string& path) {return load_rules_file(path,{});}
Result<RuleDocument> load_rules_file(const std::string& path,const syntax::IncludeOptions& input) {
  auto options=input;if(!options.resolver)options.resolver=syntax::filesystem_resolver(options.bytes);
  try {syntax::detail::IncludeContext context(options);auto source=context.resolve({},path);return load_rules(source.text,source.name,options);}
  catch(const Error& e){return Result<RuleDocument>::err(e);}
}
Result<std::string> print_rules(const RuleDocument& document) {
  auto valid=validate(document.rules);if(!valid)return Result<std::string>::err(valid.error());
  std::string out="ruleset "+sem::quote(document.name)+" {\n";std::map<std::string,uint32_t> nts(document.rules.nonterminals.begin(),document.rules.nonterminals.end());
  for(auto& [name,id]:nts)out+="  nonterminal "+name+" = "+std::to_string(id)+";\n";
  for(auto& [name,arity]:document.terminals)out+="  terminal "+name+"("+std::to_string(arity)+");\n";
   for(auto& r:document.rules.rules){Pattern p;if(r.pattern)p=*r.pattern;else{p.op=r.op;for(auto& nt:r.operands)p.children.push_back(Pattern{"",nt});}out+="  rule "+std::to_string(r.id)+" "+r.lhs+": "+print_pattern(p)+" -> "+sem::quote(r.instruction)+" cost "+std::to_string(r.cost)+" priority "+std::to_string(r.priority);if(!r.type.empty())out+=" type "+r.type;if(r.immediate)out+=" immediate ["+std::to_string(r.immediate->first)+".."+std::to_string(r.immediate->second)+"]";out+=" side_effects "+std::string(r.supports_side_effects?"true":"false")+" external_only "+(r.external_only?"true":"false")+" origin "+sem::quote(r.origin);if(!r.constraints.empty()||!r.host_constraints.empty())out+=" where "+sem::print(metacode::Value(metacode::selection_constraints_metadata(r.constraints,r.host_constraints)));if(r.fused_memory)out+=" memory_contract "+sem::print(schedrow::metadata::value(*r.fused_memory));out+=";\n";}
   out+="}\n";for(auto& tree:document.trees){out+="tree "+sem::quote(tree.name)+" {\n";for(auto& n:tree.nodes){if(n.known_constant&&(n.required||n.has_imm))return Result<std::string>::err({Error::Code::InvalidArgument,"invalid known boundary constant"});out+="  node %"+std::to_string(n.id)+" = "+n.op+"(";for(size_t k=0;k<n.children.size();++k){if(k)out+=", ";out+="%"+std::to_string(n.children[k]);}out+=")";if(!n.type.empty())out+=':'+n.type;if(n.has_imm)out+=" immediate "+std::to_string(n.imm);if(n.known_constant)out+=" known_constant "+std::to_string(*n.known_constant);if(!n.register_class.empty())out+=" register_class "+n.register_class;out+=" required "+std::string(n.required?"true":"false")+" produces_value "+(n.produces_value?"true":"false")+" side_effect "+(n.side_effect?"true":"false")+" may_trap "+(n.may_trap?"true":"false")+" call "+(n.call?"true":"false")+" terminator "+(n.terminator?"true":"false")+" origin "+sem::quote(n.origin);Object fields;if(n.access)fields["access"]=schedrow::metadata::value(*n.access);if(!n.metadata.empty()){auto payload=std::get<Object>(metacode::operand_metadata(n.metadata).data);fields["strings"]=payload.at("strings");fields["metadata"]=payload.at("properties");}if(!fields.empty())out+=" properties "+sem::print(metacode::Value(std::move(fields)));out+=";\n";}out+="  root %"+std::to_string(tree.root)+":"+tree.nonterminal+";\n}\n";}
  return Result<std::string>::ok(std::move(out));
}
}
