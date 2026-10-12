#include "infobank.hpp"
#include "schedrow/memory_metadata.hpp"
#include "target.hpp"
#include <SExprTk.hpp>
#include <charconv>
#include <climits>
#include <limits>
#include <set>

namespace limestone::limeburg {
namespace {
using Object=metacode::Value::Object;
using Array=metacode::Value::Array;
using Tree=unisel::PatternTree;
[[noreturn]] void fail(std::string message,Error::Code code=Error::Code::InvalidArgument) {throw Error{code,std::move(message)};}
std::string text(const Object& fields,const std::string& key) {auto it=fields.find(key);return it==fields.end()?"":it->second.text();}
const Object* object(const Object& fields,const std::string& key) {
  auto it=fields.find(key);if(it==fields.end())return nullptr;
  auto value=std::get_if<Object>(&it->second.data);if(!value)fail("expected object: "+key);return value;
}
bool boolean(const Object& fields,const std::string& key) {
  auto value=text(fields,key);if(value.empty()||value=="false")return false;if(value=="true")return true;fail("expected Boolean: "+key);
}
std::vector<std::string> strings(const Object& fields,const std::string& key) {
  auto it=fields.find(key);if(it==fields.end())fail("missing dataflow array: "+key,Error::Code::Unsupported);
  auto array=std::get_if<Array>(&it->second.data);if(!array)fail("expected array: "+key);
  std::vector<std::string> out;std::set<std::string> seen;
  for(auto& item:*array) {auto s=std::get_if<std::string>(&item.data);if(!s||s->empty()||!seen.insert(*s).second)fail("invalid/duplicate operand in "+key);out.push_back(*s);}
  return out;
}
std::string trim(std::string_view value) {
  auto first=value.find_first_not_of(" \t\r\n");if(first==value.npos)return {};
  return std::string(value.substr(first,value.find_last_not_of(" \t\r\n")-first+1));
}
std::map<std::string,std::string> operands(const metacode::Operation& operation) {
  auto it=operation.fields.find("operands");if(it==operation.fields.end())fail("missing operands",Error::Code::Unsupported);
  std::vector<std::string> entries;
  if(auto array=std::get_if<Array>(&it->second.data))for(auto& entry:*array) {
    auto s=std::get_if<std::string>(&entry.data);if(!s)fail("operand declaration must be textual");entries.push_back(*s);
  }else if(auto s=std::get_if<std::string>(&it->second.data);s&&!trim(*s).empty())entries.push_back(*s);
  else if(!std::holds_alternative<std::string>(it->second.data))fail("invalid operands");
  std::map<std::string,std::string> out;
  for(auto& entry:entries) {
    auto colon=entry.find(':');if(colon==entry.npos||entry.find(':',colon+1)!=entry.npos)fail("invalid operand declaration: "+entry);
    auto name=trim(std::string_view(entry).substr(0,colon)),kind=trim(std::string_view(entry).substr(colon+1));
    if(name.empty()||kind.empty()||!out.emplace(name,kind).second)fail("invalid/duplicate operand declaration: "+entry);
  }
  return out;
}
// Escape underscores as well: a literal escape-looking name cannot collide.
std::string identifier(std::string_view name) {
  constexpr char hex[]="0123456789abcdef";std::string out;
  for(unsigned char c:name)if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))out+=char(c);
  else {out+='_';out+=hex[c>>4];out+=hex[c&15];}
  return out;
}
struct Term {
  enum class Kind { Symbol,String,Integer,List } kind;
  std::string atom;int64_t integer=0;std::vector<Term> children;
};
Term term(const sexprtk::Cell& cell) {
  if(!cell.tail.empty())fail("quoted/dotted semantics require an adapter",Error::Code::Unsupported);
  if(cell.head.is_symbol())return {Term::Kind::Symbol,cell.head.as_string()};
  if(cell.head.is_string())return {Term::Kind::String,cell.head.as_string()};
  if(cell.head.is_int())return {Term::Kind::Integer,{},cell.head.as_int()};
  if(cell.head.is_list()) {
    Term out{Term::Kind::List};for(auto& child:cell.head.as_list().cells)out.children.push_back(term(child));return out;
  }
  fail("semantic atom needs a typed adapter",Error::Code::Unsupported);
}
Term parse_semantics(const metacode::Operation& operation) {
  if(operation.semantics.empty())fail("missing semantics",Error::Code::Unsupported);
  if(operation.semantics.size()>1048576)fail("semantic input exceeds 1 MiB",Error::Code::ResourceLimit);
  // Bound recursion before asking the supplied S-expression parser to allocate.
  size_t depth=0;bool quoted=false,escape=false,comment=false;
  for(char c:operation.semantics) {
    if(comment){if(c=='\n')comment=false;continue;}
    if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}
    if(c==';'){comment=true;continue;}if(c=='"'){quoted=true;continue;}
    if(c=='('&&++depth>128)fail("semantic nesting exceeds 128",Error::Code::ResourceLimit);
    if(c==')'){if(!depth)fail("unexpected semantic ')'",Error::Code::Parse);--depth;}
  }
  if(depth||quoted)fail("unterminated semantics",Error::Code::Parse);
  auto parsed=sexprtk::SExprTk{}.parse(sexprtk::Source::from_string(operation.semantics,operation.source.file));
  if(!parsed.ok()||parsed.root.size()!=1)fail("expected one semantic S-expression",Error::Code::Parse);
  return term(parsed.root.front());
}
std::string head(const Term& term) {
  if(term.kind!=Term::Kind::List||term.children.empty()||term.children[0].kind!=Term::Kind::Symbol)
    fail("semantic expression needs a symbolic operator",Error::Code::Unsupported);
  return term.children[0].atom;
}
uint32_t number(std::string_view input) {
  uint32_t out=0;auto [end,error]=std::from_chars(input.data(),input.data()+input.size(),out);
  if(error!=std::errc{}||end!=input.data()+input.size())fail("invalid unsigned encoding integer: "+std::string(input));return out;
}
std::pair<int64_t,int64_t> immediate_range(const metacode::Architecture& architecture,const metacode::Operation& operation,const std::string& name) {
  auto encoding=architecture.encodings.find(text(operation.fields,"encoding"));
  if(encoding==architecture.encodings.end())fail("no encoding contract for immediate "+name,Error::Code::Unsupported);
  const auto& fields=encoding->second;auto signedness=text(fields,"signed");
  if(signedness!="0"&&signedness!="1"&&signedness!="false"&&signedness!="true")fail("immediate signedness unspecified: "+name,Error::Code::Unsupported);
  uint32_t width=0;uint64_t present=0;bool direct=false,split=false;
  for(auto& [key,value]:fields) {
    if(key==name) {
      auto slice=value.text();auto colon=slice.find(':');
      if(colon==slice.npos)fail("immediate field has no fixed bit slice: "+name,Error::Code::Unsupported);
      number(std::string_view(slice).substr(0,colon));width=number(std::string_view(slice).substr(colon+1));direct=true;
    }else if(key.starts_with(name+"[")) {
      if(key.back()!=']')fail("malformed split immediate field");auto slice=std::string_view(key).substr(name.size()+1,key.size()-name.size()-2);auto colon=slice.find(':');
      if(colon==slice.npos)fail("malformed logical immediate slice");
      auto high=number(slice.substr(0,colon)),low=number(slice.substr(colon+1));if(high<low||high>=64)fail("invalid logical immediate slice");
      auto physical=value.text();auto physical_colon=physical.find(':');if(physical_colon==physical.npos)fail("invalid physical immediate slice");
      number(std::string_view(physical).substr(0,physical_colon));if(number(std::string_view(physical).substr(physical_colon+1))!=high-low+1)fail("logical/physical immediate widths differ");
      for(uint32_t bit=low;bit<=high;++bit){auto mask=uint64_t{1}<<bit;if(present&mask)fail("overlapping logical immediate slices");present|=mask;}
      width=std::max(width,high+1);split=true;
    }
  }
  if(direct&&split)fail("mixed direct/split immediate field");
  if(!width||width>64)fail("immediate bit width unspecified/unrepresentable: "+name,Error::Code::Unsupported);
  if(split) {
    auto full=width==64?UINT64_MAX:(uint64_t{1}<<width)-1;auto missing=full&~present;
    auto alignment=text(fields,"alignment");auto align=alignment.empty()?1u:number(alignment);
    if(!align||(align&(align-1)))fail("encoding alignment must be a power of two");
    // Only omitted low bits explicitly guaranteed by alignment are legal.
    if(missing&~uint64_t(align-1))fail("split immediate has unspecified logical bits: "+name,Error::Code::Unsupported);
  }
  bool sign=signedness=="1"||signedness=="true";
  if(sign){if(width==64)return {INT64_MIN,INT64_MAX};auto limit=int64_t{1}<<(width-1);return {-limit,limit-1};}
  if(width==64)fail("unsigned 64-bit immediate needs a wider source value adapter",Error::Code::Unsupported);
  return {0,width==63?INT64_MAX:(int64_t{1}<<width)-1};
}
unisel::Pattern inventory_pattern(const metacode::Architecture& architecture,const metacode::Operation& operation,const std::set<std::string>& classes,InstructionCoverage& coverage,RuleId id) {
  const auto* tooling=object(operation.fields,"tooling");if(!tooling)fail("missing instruction tooling",Error::Code::Unsupported);
  const auto* dataflow=object(*tooling,"dataflow");const auto* vmm=object(*tooling,"vmm");
  if(!dataflow||!vmm)fail("missing dataflow/effect contract",Error::Code::Unsupported);
  for(auto key:{"flags_read","flags_written","memory"})if(!dataflow->contains(key))fail("missing dataflow effect: "+std::string(key),Error::Code::Unsupported);
  for(auto key:{"control_flow","terminator","may_trap","atomic","serializing"})if(!vmm->contains(key))fail("missing VMM effect: "+std::string(key),Error::Code::Unsupported);
  const auto flags_read=boolean(*dataflow,"flags_read"),flags_written=boolean(*dataflow,"flags_written");
  const auto atomic=boolean(*vmm,"atomic"),serializing=boolean(*vmm,"serializing");
  auto declared=operands(operation);auto uses=strings(*dataflow,"uses"),defs=strings(*dataflow,"defs");
  auto explicit_operands=strings(*dataflow,"explicit_operands");std::set<std::string> explicit_names(explicit_operands.begin(),explicit_operands.end());
  if(explicit_names.size()!=declared.size())fail("dataflow and operand inventory differ");
  for(auto& [name,kind]:declared)if(!explicit_names.contains(name))fail("undeclared dataflow operand: "+name);
  for(auto& name:uses)if(!declared.contains(name))fail("implicit use needs a state adapter: "+name,Error::Code::Unsupported);
  for(auto& name:defs)if(!declared.contains(name))fail("implicit definition needs a state adapter: "+name,Error::Code::Unsupported);
  if(defs.size()>1)fail("multiple results need a result/state adapter",Error::Code::Unsupported);
  auto semantics=parse_semantics(operation);auto root=head(semantics);const Term* expression=&semantics;
  if(root=="set") {
    if(semantics.children.size()!=3||semantics.children[1].kind!=Term::Kind::Symbol)fail("malformed set semantics");
    auto destination=semantics.children[1].atom;
    if(defs!=std::vector<std::string>{destination})fail("semantic/dataflow definitions differ",Error::Code::Unsupported);
    if(!classes.contains(declared.at(destination)))fail("result needs a typed/virtual-ISA adapter: "+declared.at(destination),Error::Code::Unsupported);
    coverage.produces_value=true;coverage.register_class=declared.at(destination);expression=&semantics.children[2];
  }else {
    if(!defs.empty())fail("non-expression results need a result/state adapter",Error::Code::Unsupported);
    if(root=="effect"||root=="stack") {
      if(semantics.children.size()!=2)fail("malformed semantic effect wrapper");expression=&semantics.children[1];
    }
  }
  coverage.call=text(*vmm,"control_flow")=="call";coverage.terminator=boolean(*vmm,"terminator");coverage.may_trap=boolean(*vmm,"may_trap");
  coverage.side_effect=!coverage.produces_value||text(*dataflow,"memory")!="none"||flags_read||flags_written||coverage.call||coverage.terminator||coverage.may_trap||atomic||serializing;
  unisel::Pattern pattern{id,operation.name,"","",{},1};pattern.instruction=operation.name;pattern.supports_side_effects=coverage.side_effect;
  pattern.origin=operation.source.file+":"+std::to_string(operation.source.line)+":"+std::to_string(operation.source.column);
  std::set<std::string> consumed;
  size_t immediate_count=0;for(auto& name:uses)if(!classes.contains(declared.at(name)))++immediate_count;
  std::function<Tree(const Term&)> convert=[&](const Term& term)->Tree {
    Tree tree;
    if(term.kind==Term::Kind::Symbol&&declared.contains(term.atom)) {
      if(std::find(uses.begin(),uses.end(),term.atom)==uses.end())fail("semantic read absent from dataflow: "+term.atom,Error::Code::Unsupported);
      consumed.insert(term.atom);tree.binding=term.atom;auto klass=declared.at(term.atom);
      if(classes.contains(klass))tree.register_class=klass;
      else {
        if(klass!="imm"&&klass!="rel")fail("operand kind needs an adapter: "+klass,Error::Code::Unsupported);
        if(immediate_count>1)fail("multiple immediates need per-operand signedness contracts",Error::Code::Unsupported);
        tree.op="CONST";tree.immediate=immediate_range(architecture,operation,term.atom);
        auto& encoding=architecture.encodings.at(text(operation.fields,"encoding"));auto alignment=text(encoding,"alignment");
        if(!alignment.empty()){auto value=number(alignment);if(!value)fail("zero encoding alignment");pattern.constraints.push_back({metacode::OperandPredicate::MultipleOf,term.atom,{},int64_t(value)});}
      }
    }else if(term.kind==Term::Kind::Integer) {tree.op="CONST";tree.immediate=std::pair{term.integer,term.integer};}
    else if(term.kind==Term::Kind::Symbol||term.kind==Term::Kind::String)tree.op=std::string(term.kind==Term::Kind::String?"TAG_string_":"TAG_symbol_")+identifier(term.atom);
    else {
      auto op=head(term);tree.op="SEM_"+identifier(op)+"_"+std::to_string(term.children.size()-1);
      for(size_t k=1;k<term.children.size();++k)tree.inputs.push_back(convert(term.children[k]));
    }
    return tree;
  };
  pattern.tree=convert(*expression);
  if(pattern.tree->op.empty()) {Tree move;move.inputs.push_back(std::move(*pattern.tree));pattern.tree=std::move(move);}
  pattern.tree->op="ISA_"+identifier(operation.name);pattern.tree->register_class=coverage.register_class;pattern.root_op=pattern.tree->op;
  if(consumed!=std::set<std::string>(uses.begin(),uses.end()))fail("semantic expression omits declared inputs",Error::Code::Unsupported);
  if(auto selection=object(*tooling,"instruction_selection")) {
    if(boolean(*selection,"pseudo"))fail("pseudo instruction needs an expansion adapter",Error::Code::Unsupported);
    auto cost=text(*selection,"cost");if(!cost.empty()){auto value=number(cost);if(value>INT_MAX)fail("selection cost exceeds signed int");pattern.cost=int(value);}
    if(auto where=object(*selection,"where")) {
      auto constraints=metacode::load_operand_constraints(*where);if(!constraints)throw constraints.error();
      pattern.constraints.insert(pattern.constraints.end(),constraints.value().begin(),constraints.value().end());
      auto host=metacode::load_host_constraints(*where);if(!host)throw host.error();pattern.host_constraints=std::move(host.value());
    }
    if(auto memory=selection->find("memory_contract");memory!=selection->end()){auto access=schedrow::metadata::load_memory_access(memory->second);if(!access)throw access.error();pattern.fused_memory=std::move(access.value());}
  }
  return pattern;
}
void terminals(const Pattern& pattern,std::map<std::string,uint32_t>& out) {
  if(!pattern.nonterminal.empty())return;
  auto [it,added]=out.emplace(pattern.op,pattern.children.size());if(!added&&it->second!=pattern.children.size())fail("conflicting terminal arity: "+pattern.op,Error::Code::Conflict);
  for(auto& child:pattern.children)terminals(child,out);
}
std::string line(std::string input) {for(char& c:input)if(c=='\r'||c=='\n')c=' ';return input;}
}
Result<InfobankSpec> from_infobank(const metacode::Architecture& architecture) {
  try {
    if(architecture.operations.size()>=65536)fail("instruction inventory exceeds generation budget",Error::Code::ResourceLimit);
    auto normalized=unisel::from_metacode(architecture);if(!normalized)return Result<InfobankSpec>::err(normalized.error());
    InfobankSpec result;result.machine=std::move(normalized.value());result.document.name=architecture.name;
    std::set<std::string> classes;for(auto& [name,members]:result.machine.register_classes)classes.insert(name);
    auto explicit_patterns=std::move(result.machine.patterns);result.machine.patterns.clear();
    std::map<std::string,unisel::Pattern> explicit_by_instruction;for(auto& pattern:explicit_patterns)explicit_by_instruction.emplace(pattern.instruction,std::move(pattern));
    for(size_t k=0;k<architecture.operations.size();++k) {
      const auto& operation=architecture.operations[k];InstructionCoverage coverage;coverage.instruction=operation.name;
      auto id=RuleId(k+1);
      if(auto it=explicit_by_instruction.find(operation.name);it!=explicit_by_instruction.end()) {
        auto pattern=std::move(it->second);pattern.id=id;coverage.mode="explicit";coverage.rule=id;
        coverage.side_effect=pattern.supports_side_effects;result.machine.patterns.push_back(std::move(pattern));
      }else try {
        auto pattern=inventory_pattern(architecture,operation,classes,coverage,id);coverage.mode="inventory";coverage.rule=id;result.machine.patterns.push_back(std::move(pattern));
      }catch(const Error& error) {
        if(error.code!=Error::Code::Unsupported)throw Error{error.code,operation.source.file+":"+std::to_string(operation.source.line)+":"+std::to_string(operation.source.column)+": "+operation.name+": "+error.message};
        coverage.mode="unsupported";coverage.reason=error.message;coverage.rule.reset();
      }
      result.coverage.push_back(std::move(coverage));
    }
    for(auto& pattern:result.machine.patterns) {
      std::function<void(const Tree&)> collect=[&](const Tree& tree){if(tree.op.empty())return;auto [it,added]=result.machine.operators.emplace(tree.op,tree.inputs.size());if(!added&&it->second!=tree.inputs.size())fail("conflicting operator arity: "+tree.op,Error::Code::Conflict);for(auto& child:tree.inputs)collect(child);};collect(*pattern.tree);
    }
    auto rules=from_umd(result.machine);if(!rules)return Result<InfobankSpec>::err(rules.error());result.document.rules=std::move(rules.value());result.document.rules.nonterminals["stmt"]=1;
    for(auto& rule:result.document.rules.rules) {
      const auto& coverage=result.coverage.at(rule.id-1);if(coverage.mode=="inventory"&&!coverage.produces_value)rule.lhs=rule.result="stmt";
      terminals(*rule.pattern,result.document.terminals);
    }
    uint32_t next=65536;
    for(auto& klass:classes) {
      Rule rule{};rule.id=next++;rule.lhs=rule.result="value";rule.op="INPUT";rule.cost=0;rule.external_only=true;
      rule.pattern=Pattern{};rule.pattern->op="INPUT";rule.pattern->register_class=klass;
      rule.origin=architecture.source.file+":register_class:"+klass;result.document.rules.rules.push_back(std::move(rule));
      auto [it,added]=result.document.terminals.emplace("INPUT",0);if(!added&&it->second)fail("INPUT is reserved for an external leaf",Error::Code::Conflict);
    }
    auto valid=validate(result.document.rules);if(!valid)return Result<InfobankSpec>::err(valid.error());
    return Result<InfobankSpec>::ok(std::move(result));
  }catch(const Error& error){return Result<InfobankSpec>::err(error);}
}
Result<std::string> print_infobank_spec(const InfobankSpec& specification) {
  try {
  auto rendered=print_rules(specification.document);if(!rendered)return rendered;
  std::string out="# Generated by limeburg-generate-specs; edit Infobank sources, then regenerate.\n";
  out+="# Source: "+line(specification.machine.source.file)+"\n";
  out+="# Architecture: "+line(specification.machine.name)+"; family="+line(text(specification.machine.metadata,"family"))+"; model="+line(text(specification.machine.metadata,"model"))+"; version="+line(text(specification.machine.metadata,"version"))+"\n";
  out+="# Cost model: instruction count (one per inventory instruction); explicit costs override.\n";
  out+="# ISA_* terminals retain instruction identity; SEM_*/TAG_* preserve nested semantics.\n";
  out+="# INPUT is an external register value (required false); CONST carries an immediate.\n";
  out+="# Source/target adapters supply the recorded effects, ties, state, and scheduling metadata.\n";
  for(const auto& coverage:specification.coverage) {
    auto it=specification.machine.instructions.find(coverage.instruction);if(it==specification.machine.instructions.end())return Result<std::string>::err({Error::Code::NotFound,"coverage references unknown instruction"});
    const auto& fields=it->second;out+="\n# "+line(coverage.instruction)+": "+coverage.mode;
    if(coverage.rule)out+="; rule="+std::to_string(*coverage.rule);else out+="; "+line(coverage.reason);
    out+="\n# semantics: "+line(text(fields,"semantics"))+"\n";
    out+="# operands: "+line(text(fields,"operands"))+"; encoding="+line(text(fields,"encoding"))+"\n";
    if(auto tooling=object(fields,"tooling"))for(auto key:{"dataflow","register_allocation","vmm","scheduling"})if(tooling->contains(key))out+="# "+std::string(key)+": "+line(text(*tooling,key))+"\n";
  }
  out+='\n';out+=rendered.value();return Result<std::string>::ok(std::move(out));
  }catch(const Error& error){return Result<std::string>::err(error);}
}
}
