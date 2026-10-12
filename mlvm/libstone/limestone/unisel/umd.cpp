#include "umd.hpp"
#include "schedrow/memory_metadata.hpp"
#include "parsers/unisel_ast.hpp"
#include "parsers/source_internal.hpp"
#include "parsers/semantic.hpp"
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace limestone::unisel {
namespace {
namespace ast=::limestone::syntax::unisel;
using Object=metacode::Value::Object;
using V=metacode::Value;
metacode::SourceLocation location(const syntax::SourceSpan& s){return {s.file,s.begin.offset,static_cast<uint32_t>(s.begin.line),static_cast<uint32_t>(s.begin.column)};}
[[noreturn]] void fail(const ast::Node& node,std::string message,Error::Code code=Error::Code::InvalidArgument) {
  auto& s=node.source;throw Error{code,s.file+":"+std::to_string(s.begin.line)+":"+std::to_string(s.begin.column)+": "+message};
}
int64_t integer(std::string_view text) {
  int64_t value=0;int base=10;if(text.starts_with("0x")){text.remove_prefix(2);base=16;}
  auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value,base);
  if(error!=std::errc{}||end!=text.data()+text.size())throw Error{Error::Code::InvalidArgument,"integer outside signed 64-bit range"};return value;
}
uint32_t number(std::string_view text) {
  auto n=integer(text);if(n<0||uint64_t(n)>std::numeric_limits<uint32_t>::max())throw Error{Error::Code::InvalidArgument,"identifier outside 32-bit range"};return static_cast<uint32_t>(n);
}
V metadata_integer(std::string_view text) {
  if(text.starts_with('-'))return V(integer(text));
  uint64_t result=0;int base=10;if(text.starts_with("0x")){text.remove_prefix(2);base=16;}
  auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result,base);
  if(error!=std::errc{}||end!=text.data()+text.size())throw Error{Error::Code::InvalidArgument,"metadata integer outside 64-bit range"};
  if(result<=uint64_t(std::numeric_limits<int64_t>::max()))return V(static_cast<int64_t>(result));
  return V(result);
}
std::string unquote(std::string_view s) {
  return syntax::semantic::unquote(s);
}
std::string name(const ast::Name& n){return std::visit([](auto& token){auto text=token->value;return text.starts_with('"')?unquote(text):text;},n.value);}
Object attributes(const std::vector<std::unique_ptr<ast::Attribute>>&);
V value(const ast::Value& input) {
  auto result=std::visit([](auto& ptr)->V {
    using T=std::remove_cvref_t<decltype(*ptr)>;
    if constexpr(std::is_same_v<T,ast::Object>)return V(attributes(ptr->attributes));
    else if constexpr(std::is_same_v<T,ast::Array>){V::Array array;for(auto& item:ptr->items)array.push_back(value(*item));return V(std::move(array));}
    else if constexpr(std::is_same_v<T,ast::Integer>)return metadata_integer(ptr->value);
    else if constexpr(std::is_same_v<T,ast::Boolean>)return V(ptr->value=="true");
    else if constexpr(std::is_same_v<T,ast::String>)return V(unquote(ptr->value));
    else return V(ptr->value);
  },input.value);result.source=location(input.source);return result;
}
Object attributes(const std::vector<std::unique_ptr<ast::Attribute>>& input) {
  Object out;for(auto& attribute:input) {
    auto v=attribute->section?V(attributes(attribute->section->attributes)):value(*attribute->value_data);v.source=location(attribute->source);
    if(!out.emplace(name(*attribute->name),std::move(v)).second)fail(*attribute,"duplicate attribute");
  }return out;
}
std::unique_ptr<ast::Document> expand(std::string_view text,std::string_view file,syntax::detail::IncludeContext& context) {
  syntax::detail::IncludeScope scope(context,text,file);
  auto parsed=ast::parse(text,file);if(!parsed)throw parsed.error();auto document=std::move(parsed.value());
  std::vector<std::unique_ptr<ast::Declaration>> declarations;
  for(auto& declaration:document->declarations) {
    if(auto p=std::get_if<std::unique_ptr<ast::Include>>(&declaration->value)) {
      const auto& include=**p;
      try {
        auto source=context.resolve(file,unquote(include.path->value));auto nested=expand(source.text,source.name,context);
        for(auto& child:nested->declarations)declarations.push_back(std::move(child));
      }catch(const Error& e){fail(include,"include "+include.path->value+": "+e.message,e.code);}
    }else declarations.push_back(std::move(declaration));
  }
  document->declarations=std::move(declarations);return document;
}
std::string text(const Object& o,const std::string& key){auto it=o.find(key);return it==o.end()?"":it->second.text();}
PatternTree tree(const ast::TreePattern& input) {
  return std::visit([](auto& ptr)->PatternTree {
    using T=std::remove_cvref_t<decltype(*ptr)>;PatternTree result;
    if constexpr(std::is_same_v<T,ast::Integer>){auto n=integer(ptr->value);result.op="const";result.immediate=std::pair{n,n};}
    else {
      if(ptr->type)result.type=ptr->type->name->value;
      if(ptr->immediate)result.immediate=std::pair{integer(ptr->immediate->lower->value),integer(ptr->immediate->upper->value)};
      if(ptr->register_class)result.register_class=name(*ptr->register_class->name);
      if constexpr(std::is_same_v<T,ast::BindingPattern>)result.binding=ptr->binding->value.substr(1);
      else {result.op=ptr->opcode->value;if(ptr->binding)result.binding=ptr->binding->name->value;for(auto& child:ptr->children)result.inputs.push_back(tree(*child));}
    }return result;
  },input.value);
}
// Numeric references retain their IDs; symbolic references receive sorted IDs.
std::map<std::string,uint32_t> identities(const std::set<std::string>& names) {
  std::map<std::string,uint32_t> out;std::set<uint32_t> used;
  for(auto& name:names)if(name.size()>1&&name.find_first_not_of("0123456789",1)==name.npos) {
    auto id=number(std::string_view(name).substr(1));if(!used.insert(id).second)throw Error{Error::Code::Conflict,"reference spellings identify the same numeric ID"};out[name]=id;
  }
  uint64_t next=0;for(auto& name:names)if(!out.contains(name)){while(next<=std::numeric_limits<uint32_t>::max()&&used.contains(static_cast<uint32_t>(next)))++next;if(next>std::numeric_limits<uint32_t>::max())throw Error{Error::Code::ResourceLimit,"reference identity overflow"};out[name]=static_cast<uint32_t>(next);used.insert(static_cast<uint32_t>(next++));}
  return out;
}
Pattern pattern(const ast::Pattern& input,uint32_t id) {
  Pattern result{id,name(*input.name),"",name(*input.instruction),{},1};result.tree=tree(*input.tree);result.root_op=result.tree->op;
  result.origin=input.source.file+":"+std::to_string(input.source.begin.line)+":"+std::to_string(input.source.begin.column);std::set<std::string> options;
  for(auto& option:input.options)std::visit([&](auto& p) {
    using T=std::remove_cvref_t<decltype(*p)>;std::string key;
    if constexpr(std::is_same_v<T,ast::Cost>){key="cost";auto n=integer(p->amount->value);if(n<0||n>std::numeric_limits<int>::max())fail(*p,"invalid selection cost");result.cost=static_cast<int>(n);}
    else if constexpr(std::is_same_v<T,ast::SideEffects>){key="side_effects";result.supports_side_effects=p->enabled->value=="true";}
    else if constexpr(std::is_same_v<T,ast::Origin>){key="origin";result.origin=unquote(p->name->value);}
    else if constexpr(std::is_same_v<T,ast::MemoryContract>){key="memory_contract";auto memory=schedrow::metadata::load_memory_access(V(attributes(p->access->attributes)));if(!memory)fail(*p,memory.error().message,memory.error().code);result.fused_memory=std::move(memory.value());}
    else {key="where";auto fields=attributes(p->constraints->attributes);auto constraints=metacode::load_operand_constraints(fields);if(!constraints)fail(*p,constraints.error().message,constraints.error().code);result.constraints=std::move(constraints.value());auto host=metacode::load_host_constraints(fields);if(!host)fail(*p,host.error().message,host.error().code);result.host_constraints=std::move(host.value());}
    if(!options.insert(key).second)fail(*p,"duplicate pattern option: "+key,Error::Code::Conflict);
  },option->value);
  return result;
}
Program program(const ast::Program& input) {
  struct References:ast::ConstRecursiveVisitor {std::set<std::string> names;void visit(const ast::Ref& ref)override{names.insert(ref.value);}} refs;
  input.accept(refs);auto ids=identities(refs.names);Program result;std::map<std::string,uint32_t> blocks;
  bool implicit=false;for(auto& item:input.items)if(std::holds_alternative<std::unique_ptr<ast::NodeDeclaration>>(item->value))implicit=true;
  if(implicit){blocks["entry"]=0;result.blocks.push_back({0,"entry"});}
  for(auto& item:input.items)if(auto b=std::get_if<std::unique_ptr<ast::Block>>(&item->value)){auto label=name(*(*b)->name);auto id=static_cast<uint32_t>(result.blocks.size());if(!blocks.emplace(label,id).second)fail(**b,"duplicate graph block");result.blocks.push_back({id,label});}
  if(!result.blocks.empty())result.entry=result.blocks.front().id;
  auto block_ref=[&](const V& v){auto label=v.text();if(label.starts_with('%'))label.erase(0,1);if(!blocks.contains(label))throw Error{Error::Code::InvalidArgument,"unknown graph block: "+label};return blocks.at(label);};
  auto numeric_array=[&](const Object& o,const std::string& key){std::vector<uint32_t> out;auto it=o.find(key);if(it==o.end())return out;auto array=std::get_if<V::Array>(&it->second.data);if(!array)throw Error{Error::Code::InvalidArgument,"expected graph numeric array: "+key};for(auto& v:*array)out.push_back(number(v.text()));return out;};
  auto boolean=[&](const Object& o,const std::string& key){auto v=text(o,key);if(v.empty()||v=="false")return false;if(v=="true")return true;throw Error{Error::Code::InvalidArgument,"expected graph Boolean: "+key};};
  std::function<void(const std::vector<std::unique_ptr<ast::GraphItem>>&,uint32_t)> items;
  items=[&](auto& list,uint32_t block) {
    for(auto& item:list)std::visit([&](auto& ptr) {
      using T=std::remove_cvref_t<decltype(*ptr)>;
      if constexpr(std::is_same_v<T,ast::Block>) {
        if(block!=UINT32_MAX)fail(*ptr,"nested CFG blocks are unsupported",Error::Code::Unsupported);items(ptr->items,blocks.at(name(*ptr->name)));
      }else if constexpr(std::is_same_v<T,ast::NodeDeclaration>) {
        Node n{ids.at(ptr->result->value),ptr->opcode->value,{}, {},ptr->type?ptr->type->name->value:"",block==UINT32_MAX?0:block};n.origin=ptr->source.file+":"+std::to_string(ptr->source.begin.line)+":"+std::to_string(ptr->source.begin.column);
        uint64_t argument_index=0;for(auto& arg:ptr->inputs){if(argument_index>UINT32_MAX)fail(*ptr,"graph argument index overflow",Error::Code::ResourceLimit);std::visit([&](auto& operand) {
          using A=std::remove_cvref_t<decltype(*operand)>;
          if constexpr(std::is_same_v<A,ast::Ref>)n.inputs.push_back(ids.at(operand->value));
          else if constexpr(std::is_same_v<A,ast::Integer>){if(n.op!="const"||n.constant)fail(*operand,"integer arguments require a const node");n.constant=integer(operand->value);}
          else n.metadata.strings.push_back({uint32_t(argument_index),unquote(operand->value)});
        },arg->value);++argument_index;}
        for(auto& option:ptr->options)std::visit([&](auto& p){using O=std::remove_cvref_t<decltype(*p)>;
          if constexpr(std::is_same_v<O,ast::NodeProperties>) {
            auto fields=attributes(p->properties->attributes);n.register_class=text(fields,"class");if(fields.contains("origin"))n.origin=text(fields,"origin");n.call=boolean(fields,"call");n.terminator=boolean(fields,"terminator");n.may_trap=boolean(fields,"may_trap");
            std::map<std::string,schedrow::ControlFlow> flow{{"",schedrow::ControlFlow::None},{"none",schedrow::ControlFlow::None},{"branch",schedrow::ControlFlow::Branch},{"conditional_branch",schedrow::ControlFlow::ConditionalBranch},{"return",schedrow::ControlFlow::Return},{"indirect_branch",schedrow::ControlFlow::IndirectBranch},{"trap",schedrow::ControlFlow::Trap},{"call",schedrow::ControlFlow::Call}};auto control=text(fields,"control_flow");if(!flow.contains(control))fail(*p,"unknown graph control flow");n.control=flow.at(control);
            if(auto it=fields.find("targets");it!=fields.end()){auto array=std::get_if<V::Array>(&it->second.data);if(!array)fail(*p,"targets must be an array");for(auto& target:*array)n.block_targets.push_back(block_ref(target));}
             if(auto it=fields.find("access");it!=fields.end()){auto a=std::get_if<Object>(&it->second.data);if(!a)fail(*p,"access must be an object");schedrow::MemoryAccess memory{boolean(*a,"read"),boolean(*a,"write"),boolean(*a,"volatile"),boolean(*a,"atomic")};memory.address_space=text(*a,"address_space");memory.alias_sets=numeric_array(*a,"alias_sets");if(a->contains("size"))memory.size=number(text(*a,"size"));if(a->contains("alignment"))memory.alignment=number(text(*a,"alignment"));std::map<std::string,schedrow::MemoryOrdering> orderings{{"",schedrow::MemoryOrdering::Relaxed},{"relaxed",schedrow::MemoryOrdering::Relaxed},{"acquire",schedrow::MemoryOrdering::Acquire},{"release",schedrow::MemoryOrdering::Release},{"acq_rel",schedrow::MemoryOrdering::AcquireRelease},{"seq_cst",schedrow::MemoryOrdering::Sequential}};auto ordering=text(*a,"ordering");if(!orderings.contains(ordering))fail(*p,"unknown memory ordering");memory.ordering=orderings.at(ordering);n.access=std::move(memory);for(auto& [key,v]:*a)if(key!="read"&&key!="write"&&key!="volatile"&&key!="atomic"&&key!="address_space"&&key!="alias_sets"&&key!="size"&&key!="alignment"&&key!="ordering")fail(*p,"unknown graph memory property: "+key);}
             if(auto it=fields.find("metadata");it!=fields.end()){auto properties=std::get_if<Object>(&it->second.data);if(!properties)fail(*p,"graph metadata must be an object");n.metadata.properties=*properties;}
             for(auto& [key,v]:fields)if(key!="class"&&key!="origin"&&key!="call"&&key!="terminator"&&key!="may_trap"&&key!="control_flow"&&key!="targets"&&key!="access"&&key!="metadata")fail(*p,"unknown graph node property: "+key);
          }else{bool enabled=p->enabled->value=="true";if constexpr(std::is_same_v<O,ast::Required>)n.required=enabled;else if constexpr(std::is_same_v<O,ast::ProducesValue>)n.produces_value=enabled;else n.side_effect=enabled;}
        },option->value);
        result.nodes.push_back(std::move(n));
      }else if constexpr(std::is_same_v<T,ast::Output>){for(auto& ref:ptr->values)if(block==UINT32_MAX)result.outputs.push_back(ids.at(ref->value));else result.blocks[block].live_out.push_back(ids.at(ref->value));}
      else if constexpr(std::is_same_v<T,ast::Dependency>) {
        auto fields=attributes(ptr->attributes);auto a=text(fields,"producer"),b=text(fields,"consumer");
        if(!ids.contains(a)||!ids.contains(b))fail(*ptr,"dependency needs known producer and consumer references");
        std::map<std::string,schedrow::DepKind> kinds{{"true",schedrow::DepKind::True},{"anti",schedrow::DepKind::Anti},{"output",schedrow::DepKind::Output},{"memory",schedrow::DepKind::Memory},{"control",schedrow::DepKind::Control},{"ordering",schedrow::DepKind::Ordering}};
        auto kind=text(fields,"kind");if(!kinds.contains(kind))fail(*ptr,"unknown dependency kind");
        result.dependencies.push_back({ids.at(a),ids.at(b),kinds.at(kind),fields.contains("latency")?number(text(fields,"latency")):0,fields.contains("distance")?number(text(fields,"distance")):0,text(fields,"scheduler_only")=="true"});
        for(auto& [key,v]:fields)if(key!="producer"&&key!="consumer"&&key!="kind"&&key!="latency"&&key!="distance"&&key!="scheduler_only")fail(*ptr,"unknown dependency attribute: "+key);
      }else {
        auto key=name(*ptr->name);auto v=ptr->section?V(attributes(ptr->section->attributes)):value(*ptr->value_data);
        if(key=="entry"&&block==UINT32_MAX)result.entry=block_ref(v);
        else if(key=="successors"){auto array=std::get_if<V::Array>(&v.data);if(!array)fail(*ptr,"successors must be an array");if(block==UINT32_MAX&&!implicit)fail(*ptr,"successors require a block");auto& successors=result.blocks[block==UINT32_MAX?0:block].successors;for(auto& next:*array)successors.push_back(block_ref(next));}
        else fail(*ptr,"unsupported graph attribute: "+key,Error::Code::Unsupported);
      }
    },item->value);
  };
  items(input.items,UINT32_MAX);if(implicit&&result.blocks.size()==1)result.blocks.clear();return result;
}
std::string quote(const std::string& s){return syntax::semantic::quote(s);}
std::string render(const PatternTree& tree) {
  std::string out=tree.op.empty()?"?"+tree.binding:tree.op;
  if(!tree.op.empty()){out+='(';for(size_t k=0;k<tree.inputs.size();++k){if(k)out+=',';out+=render(tree.inputs[k]);}out+=')';}
  if(!tree.type.empty())out+=":"+tree.type;
  if(tree.immediate)out+="["+std::to_string(tree.immediate->first)+".."+std::to_string(tree.immediate->second)+"]";
  if(!tree.register_class.empty())out+=" register_class "+quote(tree.register_class);
  if(!tree.op.empty()&&!tree.binding.empty())out+=" binding "+tree.binding;
  return out;
}
std::string render_value(const V& v);
std::string render_object(const Object& o) {
  std::map<std::string,V> sorted(o.begin(),o.end());std::string out="{";
  for(auto& [key,v]:sorted)out+=quote(key)+"="+render_value(v)+";";return out+"}";
}
std::string render_value(const V& v) {
  if(auto object=std::get_if<Object>(&v.data))return render_object(*object);
  if(auto array=std::get_if<V::Array>(&v.data)){std::string out="[";for(size_t k=0;k<array->size();++k){if(k)out+=',';out+=render_value((*array)[k]);}return out+"]";}
  if(auto s=std::get_if<std::string>(&v.data))return quote(*s);return v.text();
}
}
Result<int> validate(const MachineDescription& machine) {
  if(machine.name.empty())return Result<int>::err({Error::Code::InvalidArgument,"machine description has no identity"});
  auto valid=validate(Program{},machine.patterns);if(!valid)return valid;
  for(auto& pattern:machine.patterns) {
    if(!machine.instructions.contains(pattern.instruction))return Result<int>::err({Error::Code::NotFound,"pattern references unknown instruction: "+pattern.instruction});
    std::function<bool(const PatternTree&)> check=[&](auto& tree){if(!tree.register_class.empty()&&!machine.register_classes.contains(tree.register_class))return false;if(tree.op.empty())return true;auto it=machine.operators.find(tree.op);return it!=machine.operators.end()&&it->second==tree.inputs.size()&&std::all_of(tree.inputs.begin(),tree.inputs.end(),check);};
    if(!check(pattern_tree(pattern)))return Result<int>::err({Error::Code::InvalidArgument,"unknown operator/register class or wrong pattern arity: "+pattern.name});
  }
  std::set<uint32_t> physical;for(auto& [name,id]:machine.physical_names)if(name.empty()||!physical.insert(id).second)return Result<int>::err({Error::Code::Conflict,"invalid physical register identity"});
  for(auto& [name,regs]:machine.register_classes){std::set<uint32_t> members;for(auto reg:regs)if(!physical.contains(reg)||!members.insert(reg).second)return Result<int>::err({Error::Code::InvalidArgument,"unknown or duplicate register-class member"});}
  for(auto [a,b]:machine.aliases)if(!physical.contains(a)||!physical.contains(b))return Result<int>::err({Error::Code::InvalidArgument,"unknown physical alias"});
  return Result<int>::ok(0);
}
Result<Document> load_umd(std::string_view text,std::string_view file) {
  return load_umd(text,file,{});
}
Result<Document> load_umd(std::string_view text,std::string_view file,const syntax::IncludeOptions& options) {
  try {
    syntax::detail::IncludeContext context(options);auto parsed=expand(text,file,context);
    Document result;const ast::Machine* machine=nullptr;const ast::Program* source=nullptr;
    for(auto& decl:parsed->declarations)std::visit([&](auto& ptr){using T=std::remove_cvref_t<decltype(*ptr)>;if constexpr(std::is_same_v<T,ast::Machine>){if(machine)fail(*ptr,"multiple machines in one UMD");machine=ptr.get();}else if constexpr(std::is_same_v<T,ast::Program>){if(source)fail(*ptr,"multiple source programs");source=ptr.get();}else fail(*ptr,"unexpanded include",Error::Code::Internal);},decl->value);
    if(!machine)throw Error{Error::Code::InvalidArgument,"UMD has no machine declaration"};auto& target=result.machine;target.name=name(*machine->name);target.source=location(machine->source);
    std::set<std::string> physical;for(auto& item:machine->items)std::visit([&](auto& ptr){using T=std::remove_cvref_t<decltype(*ptr)>;if constexpr(std::is_same_v<T,ast::RegisterClass>)for(auto& reg:ptr->members)physical.insert(reg->value);else if constexpr(std::is_same_v<T,ast::RegisterAlias>){physical.insert(ptr->first->value);physical.insert(ptr->second->value);}},item->value);
    auto ids=identities(physical);for(auto& [name,id]:ids)target.physical_names[name.substr(1)]=id;
    std::set<uint32_t> rule_ids;for(auto& item:machine->items)if(auto p=std::get_if<std::unique_ptr<ast::Pattern>>(&item->value);p&&(*p)->number)if(!rule_ids.insert(number((*p)->number->value)).second)fail(**p,"duplicate pattern ID");
    uint32_t next=0;
    for(auto& item:machine->items)std::visit([&](auto& ptr){using T=std::remove_cvref_t<decltype(*ptr)>;
      if constexpr(std::is_same_v<T,ast::RegisterClass>){std::vector<uint32_t> members;for(auto& p:ptr->members)members.push_back(ids.at(p->value));if(!target.register_classes.emplace(name(*ptr->name),std::move(members)).second)fail(*ptr,"duplicate register class");}
      else if constexpr(std::is_same_v<T,ast::RegisterAlias>)target.aliases.emplace_back(ids.at(ptr->first->value),ids.at(ptr->second->value));
      else if constexpr(std::is_same_v<T,ast::Operator>){if(!target.operators.emplace(ptr->name->value,number(ptr->arity->value)).second)fail(*ptr,"duplicate operator");}
      else if constexpr(std::is_same_v<T,ast::Instruction>){if(!target.instructions.emplace(name(*ptr->name),attributes(ptr->attributes)).second)fail(*ptr,"duplicate instruction");}
      else if constexpr(std::is_same_v<T,ast::Pattern>){while(rule_ids.contains(next)){if(next==std::numeric_limits<uint32_t>::max())fail(*ptr,"pattern ID overflow",Error::Code::ResourceLimit);++next;}auto id=ptr->number?number(ptr->number->value):next;rule_ids.insert(id);target.patterns.push_back(pattern(*ptr,id));}
      else {std::vector<std::unique_ptr<ast::Attribute>> unused;auto key=name(*ptr->name);auto v=ptr->section?V(attributes(ptr->section->attributes)):value(*ptr->value_data);if(!target.metadata.emplace(key,std::move(v)).second)fail(*ptr,"duplicate machine attribute");}
    },item->value);
    auto checked=validate(target);if(!checked)return Result<Document>::err(checked.error());
    if(source){result.program=program(*source);auto checked=validate(*result.program,target.patterns);if(!checked)return Result<Document>::err(checked.error());}
    return Result<Document>::ok(std::move(result));
  }catch(const Error& e){return Result<Document>::err(e);}
}
Result<Document> load_umd_file(const std::string& path) {
  return load_umd_file(path,{});
}
Result<Document> load_umd_file(const std::string& path,const syntax::IncludeOptions& input) {
  auto options=input;if(!options.resolver)options.resolver=syntax::filesystem_resolver(options.bytes);
  try {syntax::detail::IncludeContext context(options);auto source=context.resolve({},path);return load_umd(source.text,source.name,options);}
  catch(const Error& e){return Result<Document>::err(e);}
}
Result<MachineDescription> from_metacode(const metacode::Architecture& architecture) {
  MachineDescription machine;machine.name=architecture.name;machine.source=architecture.source;machine.metadata=architecture.fields;
  machine.metadata["family"]=V(architecture.family);machine.metadata["model"]=V(architecture.model);machine.metadata["version"]=V(architecture.version);
  V::Array registers;std::set<std::string> names;
  for(auto& r:architecture.registers){names.insert("$"+r.name);registers.emplace_back(Object{{"name",V(r.name)},{"class",V(r.klass)},{"width",V(int64_t(r.width))},{"number",V(int64_t(r.number))}});}
  auto ids=identities(names);for(auto& r:architecture.registers){auto id=ids.at("$"+r.name);machine.physical_names[r.name]=id;machine.register_classes[r.klass].push_back(id);}
  // Virtual ISAs declare logical classes with no physical register inventory.
  // Preserve those categories instead of dropping them during normalization.
  if(auto profile=architecture.fields.find("profile");profile!=architecture.fields.end()) {
    auto fields=std::get_if<Object>(&profile->second.data);if(!fields)return Result<MachineDescription>::err({Error::Code::InvalidArgument,"profile must be an object"});
    if(auto classes=fields->find("register_classes");classes!=fields->end()) {
      auto array=std::get_if<V::Array>(&classes->second.data);if(!array)return Result<MachineDescription>::err({Error::Code::InvalidArgument,"profile register_classes must be an array"});
      std::set<std::string> seen;
      for(auto& entry:*array){auto name=std::get_if<std::string>(&entry.data);if(!name||name->empty()||!seen.insert(*name).second)return Result<MachineDescription>::err({Error::Code::InvalidArgument,"invalid/duplicate profile register class"});machine.register_classes.try_emplace(*name);}
    }
  }
  machine.metadata["registers"]=V(std::move(registers));
  Object aliases;for(auto& [a,b]:architecture.aliases){aliases[a]=V(b);if(machine.physical_names.contains(a)&&machine.physical_names.contains(b))machine.aliases.emplace_back(machine.physical_names.at(a),machine.physical_names.at(b));}machine.metadata["register_aliases"]=V(std::move(aliases));
  Object encodings;for(auto& [name,fields]:architecture.encodings)encodings[name]=V(fields);machine.metadata["encodings"]=V(std::move(encodings));
  for(auto& op:architecture.operations) {
    machine.instructions[op.name]=op.fields;auto& fields=machine.instructions.at(op.name);
    fields["source_file"]=V(op.source.file);fields["source_line"]=V(int64_t(op.source.line));fields["source_column"]=V(int64_t(op.source.column));
    auto tooling=op.fields.find("tooling");if(tooling==op.fields.end())continue;auto object=std::get_if<Object>(&tooling->second.data);if(!object)continue;
    auto selection=object->find("instruction_selection");if(selection==object->end())continue;auto contract=std::get_if<Object>(&selection->second.data);if(!contract||!contract->contains("selection_tree"))continue;
    // Parse an explicit UMD tree through the authoritative grammar.
    auto definition="machine imported { instruction "+quote(op.name)+" {} pattern 0 imported: "+text(*contract,"selection_tree")+" -> "+quote(op.name)+" cost "+(contract->contains("cost")?text(*contract,"cost"):"1")+"; }";
    auto syntax=ast::parse(definition,op.source.file);if(!syntax)return Result<MachineDescription>::err(syntax.error());
    try {
      const auto& ast_machine=*std::get<std::unique_ptr<ast::Machine>>(syntax.value()->declarations.front()->value);
      const auto& ast_pattern=*std::get<std::unique_ptr<ast::Pattern>>(ast_machine.items.back()->value);
      auto p=pattern(ast_pattern,static_cast<uint32_t>(machine.patterns.size()));p.supports_side_effects=text(*contract,"side_effects")=="true";p.origin=op.source.file+":"+std::to_string(op.source.line)+":"+std::to_string(op.source.column);
      if(auto where=contract->find("where");where!=contract->end()){auto fields=std::get_if<Object>(&where->second.data);if(!fields)throw Error{Error::Code::InvalidArgument,"instruction-selection where must be an object"};auto constraints=metacode::load_operand_constraints(*fields);if(!constraints)throw constraints.error();p.constraints=std::move(constraints.value());auto host=metacode::load_host_constraints(*fields);if(!host)throw host.error();p.host_constraints=std::move(host.value());}
      if(auto memory=contract->find("memory_contract");memory!=contract->end()){auto access=schedrow::metadata::load_memory_access(memory->second);if(!access)throw access.error();p.fused_memory=std::move(access.value());}
      std::function<void(const PatternTree&)> collect=[&](auto& t){if(t.op.empty())return;auto [it,added]=machine.operators.emplace(t.op,t.inputs.size());if(!added&&it->second!=t.inputs.size())throw Error{Error::Code::Conflict,"conflicting source operator arity"};for(auto& child:t.inputs)collect(child);};collect(*p.tree);machine.patterns.push_back(std::move(p));
    }catch(const Error& error){return Result<MachineDescription>::err(error);}
  }
  auto valid=validate(machine);if(!valid)return Result<MachineDescription>::err(valid.error());return Result<MachineDescription>::ok(std::move(machine));
}
std::string print_umd(const MachineDescription& machine) {
  std::string out="machine "+quote(machine.name)+" {\n";std::map<uint32_t,std::string> names;for(auto& [name,id]:machine.physical_names)names[id]="$"+name;
  std::map<std::string,std::vector<uint32_t>> classes(machine.register_classes.begin(),machine.register_classes.end());
  for(auto& [name,members]:classes){out+="  regclass "+quote(name)+" = [";auto regs=members;std::sort(regs.begin(),regs.end());for(size_t k=0;k<regs.size();++k){if(k)out+=',';out+=names.at(regs[k]);}out+="];\n";}
  auto aliases=machine.aliases;std::sort(aliases.begin(),aliases.end());for(auto [a,b]:aliases)out+="  alias "+names.at(a)+" = "+names.at(b)+";\n";
  std::map<std::string,size_t> operators(machine.operators.begin(),machine.operators.end());for(auto& [name,arity]:operators)out+="  operator "+name+"("+std::to_string(arity)+");\n";
  std::map<std::string,Object> instructions(machine.instructions.begin(),machine.instructions.end());for(auto& [name,fields]:instructions)out+="  instruction "+quote(name)+" "+render_object(fields)+"\n";
  auto patterns=machine.patterns;std::sort(patterns.begin(),patterns.end(),[](auto& a,auto& b){return a.id<b.id;});
  for(auto& p:patterns){
    auto t=pattern_tree(p);std::set<std::string> bindings;
    std::function<void(const PatternTree&)> collect=[&](auto& tree){if(!tree.binding.empty())bindings.insert(tree.binding);for(auto& child:tree.inputs)collect(child);};collect(t);
    size_t next=0;std::function<void(PatternTree&)> bind=[&](auto& tree){if(tree.op.empty()&&tree.binding.empty()){std::string candidate;do{candidate="arg"+std::to_string(next++);}while(bindings.contains(candidate));tree.binding=candidate;bindings.insert(candidate);}for(auto& child:tree.inputs)bind(child);};bind(t);
    out+="  pattern "+std::to_string(p.id)+" "+quote(p.name)+": "+render(t)+" -> "+quote(p.instruction)+" cost "+std::to_string(p.cost)+" side_effects "+(p.supports_side_effects?"true":"false")+" origin "+quote(p.origin);
    if(!p.constraints.empty()||!p.host_constraints.empty())out+=" where "+render_object(metacode::selection_constraints_metadata(p.constraints,p.host_constraints));if(p.fused_memory)out+=" memory_contract "+render_object(std::get<Object>(schedrow::metadata::value(*p.fused_memory).data));out+=";\n";
  }
  std::map<std::string,V> metadata(machine.metadata.begin(),machine.metadata.end());for(auto& [key,v]:metadata)out+="  "+quote(key)+"="+render_value(v)+";\n";
  return out+"}\n";
}
}
