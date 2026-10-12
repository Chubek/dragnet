#include "native_c.hpp"
#include <cctype>
#include <set>
#include <sstream>
#include <iomanip>

namespace limestone::machineir_native {
namespace {
[[noreturn]] void fail(std::string message, Error::Code code = Error::Code::InvalidArgument) {
  throw Error{code, "MachineIR native C: " + message};
}
bool identifier(std::string_view s) {
  if(s.empty() || !std::isalpha(static_cast<unsigned char>(s.front()))) return false;
  return std::all_of(s.begin(), s.end(), [](unsigned char c){return c < 128 && (std::isalnum(c) || c == '_');});
}
std::string type(const std::string& s) {
  if(s == "ptr") return "void *";
  if(s == "i32") return "int32_t";
  if(s == "u64") return "uint64_t";
  fail("unsupported C ABI value type '" + s + "'", Error::Code::Unsupported);
}
std::string quote(std::string_view s) {
  std::ostringstream o; o << '"';
  for(unsigned char c : s) {
    if(c == '"' || c == '\\') o << '\\' << static_cast<char>(c);
    else if(c < 32 || c >= 127) o << '\\' << std::oct << std::setw(3) << std::setfill('0') << unsigned(c) << std::dec;
    else o << static_cast<char>(c);
  }
  o << '"'; return o.str();
}
using V = metacode::Value;
using O = V::Object;
using A = V::Array;
const O& object(const V& v) {auto p = std::get_if<O>(&v.data); if(!p) fail("expected object", Error::Code::Parse); return *p;}
const A& array(const V& v) {auto p = std::get_if<A>(&v.data); if(!p) fail("expected array", Error::Code::Parse); return *p;}
std::string string(const V& v) {auto p = std::get_if<std::string>(&v.data); if(!p) fail("expected string", Error::Code::Parse); return *p;}
uint32_t number(const V& v) {
  uint64_t n;
  if(auto p = std::get_if<uint64_t>(&v.data)) n = *p;
  else if(auto p = std::get_if<int64_t>(&v.data); p && *p >= 0) n = static_cast<uint64_t>(*p);
  else fail("expected unsigned integer", Error::Code::Parse);
  if(n > UINT32_MAX) fail("integer overflow", Error::Code::Parse);
  return static_cast<uint32_t>(n);
}
const V& get(const O& o, const char* k) {auto p=o.find(k); if(p==o.end()) fail(std::string("missing field '")+k+"'", Error::Code::Parse); return p->second;}
void keys(const O& o, std::initializer_list<std::string_view> allowed) {
  for(const auto& [k,v] : o) if(std::find(allowed.begin(), allowed.end(), k) == allowed.end()) fail("unknown field '"+k+"'", Error::Code::Parse);
}
}
Result<int> verify(const Unit& u) {
  try {
    auto exchange = machineir_bridge::serialize(u.entry);
    if(!exchange) return Result<int>::err(exchange.error());
    if(!identifier(u.entry.function) || u.entry.region.blocks.empty()) fail("entry needs a C identifier and explicit CFG");
    if(u.entry.allocation || u.entry.frame_size || !u.entry.spill_slots.empty() || !u.entry.schedule.empty()) fail("C ABI lowering requires unallocated, unscheduled virtual MachineIR", Error::Code::Unsupported);
    if(u.support_c.find('\0') != std::string::npos) fail("NUL in C support source");
    std::map<uint32_t, std::string> types;
    std::set<uint32_t> defined, params, calls;
    for(const auto& v : u.entry.values) {type(v.type); types.emplace(v.id, v.type);}
    for(const auto& i : u.entry.region.instructions) for(auto d : i.defs) defined.insert(d);
    for(auto id : u.parameters) if(!types.contains(id) || defined.contains(id) || !params.insert(id).second) fail("invalid entry parameter");
    for(const auto& v : u.entry.values) if(!defined.contains(v.id) && !params.contains(v.id)) fail("undeclared entry input");
    for(const auto& [name,f] : u.functions) {
      if(name != f.name || !identifier(name) || name == u.entry.function) fail("invalid foreign function name");
      type(f.result_type); for(const auto& t : f.argument_types) type(t);
      if(f.body.find('\0') != std::string::npos || f.file.find('\0') != std::string::npos || f.line > INT32_MAX) fail("invalid foreign source/provenance");
    }
    std::map<uint32_t, const schedrow::Instruction*> last;
    for(const auto& i : u.entry.region.instructions) {
      last[i.block] = &i;
      if(!i.implicit_defs.empty() || !i.implicit_uses.empty() || !i.early_defs.empty() || !i.ties.empty() || !i.register_classes.empty() || !i.source_metadata.empty()) fail("C ABI lowering cannot consume implicit/fixed operand contracts", Error::Code::Unsupported);
      if(!i.immediates.empty() && i.opcode != "const.i32") fail("unexpected immediate");
      if(i.opcode == "foreign.call") {
        calls.insert(i.id);
        if(!u.callees.contains(i.id) || !u.functions.contains(u.callees.at(i.id))) fail("unbound foreign call");
        const auto& f = u.functions.at(u.callees.at(i.id));
        if(i.defs.size() != 1 || types.at(i.defs[0]) != f.result_type || i.uses.size() != f.argument_types.size() || !i.call || i.terminator || i.control != schedrow::ControlFlow::Call || i.speculative || !i.barrier || !i.memory || !i.may_trap || !i.access || !i.access->read || !i.access->write) fail("foreign call ABI/effect mismatch");
        for(size_t k=0;k<i.uses.size();++k) if(types.at(i.uses[k]) != f.argument_types[k]) fail("foreign call argument type mismatch");
      } else if(i.opcode == "const.i32") {
        if(i.defs.size()!=1 || !i.uses.empty() || i.immediates.size()!=1 || i.immediates[0].first!=i.defs[0] || types.at(i.defs[0])!="i32" || i.immediates[0].second<INT32_MIN || i.immediates[0].second>INT32_MAX) fail("invalid i32 constant");
      } else if(i.opcode == "eq.i32" || i.opcode == "lt_zero.i32") {
        if(i.defs.size()!=1 || i.uses.size()!=(i.opcode=="eq.i32"?2u:1u) || types.at(i.defs[0])!="i32") fail("invalid comparison");
        for(auto v : i.uses) if(types.at(v)!="i32") fail("comparison operand type mismatch");
      } else if(i.opcode == "br.nonzero") {
        if(i.defs.size() || i.uses.size()!=1 || types.at(i.uses[0])!="i32" || i.control!=schedrow::ControlFlow::ConditionalBranch || i.block_targets.size()!=2) fail("invalid conditional branch");
      } else if(i.opcode == "br") {
        if(i.defs.size() || i.uses.size() || i.control!=schedrow::ControlFlow::Branch || i.block_targets.size()!=1) fail("invalid branch");
      } else if(i.opcode == "ret.i32") {
        if(i.defs.size() || i.uses.size()!=1 || types.at(i.uses[0])!="i32" || i.control!=schedrow::ControlFlow::Return) fail("invalid return");
      } else fail("unsupported operation '"+i.opcode+"'", Error::Code::Unsupported);
      if(i.opcode != "foreign.call" && i.call) fail("call effects on non-call operation");
      if(i.opcode != "foreign.call" && (i.memory || i.access || i.may_trap || i.barrier)) fail("unsupported side effects on value/control operation", Error::Code::Unsupported);
      if(i.opcode == "const.i32" || i.opcode == "eq.i32" || i.opcode == "lt_zero.i32")
        if(i.terminator || i.control!=schedrow::ControlFlow::None) fail("control effect on value operation");
    }
    if(calls.size()!=u.callees.size()) fail("extraneous call binding");
    for(const auto& b : u.entry.region.blocks) if(!last.contains(b.id) || !last.at(b.id)->terminator) fail("every native block requires a terminator");
    return Result<int>::ok(0);
  } catch(const Error& e) {return Result<int>::err(e);}
}
Result<std::string> serialize(const Unit& u) {
  auto valid=verify(u); if(!valid) return Result<std::string>::err(valid.error());
  auto region=machineir_bridge::serialize(u.entry); if(!region) return region;
  A parameters, functions, calls;
  for(auto id : u.parameters) parameters.emplace_back(uint64_t(id));
  for(const auto& [name,f] : u.functions) {
    A arguments; for(const auto& t:f.argument_types) arguments.emplace_back(t);
    functions.emplace_back(O{{"name",V(name)},{"result",V(f.result_type)},{"arguments",V(std::move(arguments))},{"body",V(f.body)},{"file",V(f.file)},{"line",V(uint64_t(f.line))}});
  }
  for(const auto& [id,name] : u.callees) calls.emplace_back(O{{"instruction",V(uint64_t(id))},{"function",V(name)}});
  auto result=metacode::print_json(V(O{{"schema",V(std::string("limestone.machineir.native-c"))},{"version",V(uint64_t(1))},{"region",V(region.value())},{"parameters",V(std::move(parameters))},{"support",V(u.support_c)},{"functions",V(std::move(functions))},{"callees",V(std::move(calls))}}));
  if(result && result.value().size()>4*1024*1024) return Result<std::string>::err({Error::Code::ResourceLimit,"MachineIR native C: serialized unit exceeds 4 MiB"});
  return result;
}
Result<Unit> deserialize(std::string_view text, std::string_view file) {
  if(text.size()>4*1024*1024) return Result<Unit>::err({Error::Code::ResourceLimit,"MachineIR native C: input exceeds 4 MiB"});
  auto parsed=metacode::parse_json(text,file); if(!parsed) return Result<Unit>::err(parsed.error());
  try {
    const auto& o=object(parsed.value()); keys(o,{"schema","version","region","parameters","support","functions","callees"});
    if(string(get(o,"schema"))!="limestone.machineir.native-c" || number(get(o,"version"))!=1) fail("unsupported schema", Error::Code::Unsupported);
    Unit u; auto region=machineir_bridge::deserialize(string(get(o,"region")),file); if(!region) return Result<Unit>::err(region.error()); u.entry=std::move(region.value());
    u.support_c=string(get(o,"support")); for(const auto& v:array(get(o,"parameters"))) u.parameters.push_back(number(v));
    for(const auto& v:array(get(o,"functions"))) {
      const auto& f=object(v); keys(f,{"name","result","arguments","body","file","line"});
      ForeignFunction function; function.name=string(get(f,"name")); function.result_type=string(get(f,"result")); function.body=string(get(f,"body")); function.file=string(get(f,"file")); function.line=number(get(f,"line"));
      for(const auto& t:array(get(f,"arguments"))) function.argument_types.push_back(string(t));
      auto name=function.name; if(!u.functions.emplace(name,std::move(function)).second) fail("duplicate foreign binding", Error::Code::Conflict);
    }
    for(const auto& v:array(get(o,"callees"))) {
      const auto& c=object(v); keys(c,{"instruction","function"});
      if(!u.callees.emplace(number(get(c,"instruction")),string(get(c,"function"))).second) fail("duplicate call binding", Error::Code::Conflict);
    }
    auto valid=verify(u); if(!valid) return Result<Unit>::err(valid.error()); return Result<Unit>::ok(std::move(u));
  } catch(const Error& e) {return Result<Unit>::err(e);}
}
Result<std::string> emit_c(const Unit& u) {
  auto valid=verify(u); if(!valid) return Result<std::string>::err(valid.error());
  std::ostringstream o; o<<"/* Lowered from validated MachineIR native-C v1. */\n#include <stdint.h>\n"<<u.support_c<<'\n';
  for(const auto& [name,f] : u.functions) {
    o<<"static "<<type(f.result_type)<<' '<<name<<'(';
    if(f.argument_types.empty()) o<<"void";
    for(size_t k=0;k<f.argument_types.size();++k) {if(k) o<<", "; o<<type(f.argument_types[k])<<" a"<<k;}
    o<<") {\n";
    for(size_t k=0;k<f.argument_types.size();++k) o<<"(void)a"<<k<<";\n";
    if(!f.file.empty() && f.line) o<<"#line "<<f.line<<' '<<quote(f.file)<<'\n';
    o<<f.body<<"\n#line 1 \"<MachineIR native binding>\"\n}\n";
  }
  std::map<uint32_t,std::string> types; for(const auto& v:u.entry.values) types[v.id]=v.type;
  o<<"int32_t "<<u.entry.function<<'(';
  if(u.parameters.empty()) o<<"void";
  for(size_t k=0;k<u.parameters.size();++k) {if(k) o<<", "; auto id=u.parameters[k]; o<<type(types.at(id))<<" v"<<id;}
  o<<") {\n";
  std::set<uint32_t> params(u.parameters.begin(),u.parameters.end());
  auto values=u.entry.values; std::sort(values.begin(),values.end(),[](const auto& a,const auto& b){return a.id<b.id;});
  for(const auto& v:values) if(!params.contains(v.id)) o<<"  "<<type(v.type)<<" v"<<v.id<<";\n";
  o<<"  goto b"<<u.entry.region.entry<<";\n";
  std::map<uint32_t,const schedrow::Instruction*> instructions;
  for(const auto& i:u.entry.region.instructions) instructions[i.id]=&i;
  for(const auto& b:u.entry.region.blocks) {
    o<<"b"<<b.id<<":\n";
    for(auto id:u.entry.order) {
      const auto& i=*instructions.at(id); if(i.block!=b.id) continue;
      if(i.opcode=="foreign.call") {
        o<<"  v"<<i.defs[0]<<" = "<<u.callees.at(id)<<'(';
        for(size_t k=0;k<i.uses.size();++k) {if(k) o<<", "; o<<'v'<<i.uses[k];} o<<");\n";
      } else if(i.opcode=="const.i32") o<<"  v"<<i.defs[0]<<" = (int32_t)("<<i.immediates[0].second<<"LL);\n";
      else if(i.opcode=="eq.i32") o<<"  v"<<i.defs[0]<<" = v"<<i.uses[0]<<" == v"<<i.uses[1]<<";\n";
      else if(i.opcode=="lt_zero.i32") o<<"  v"<<i.defs[0]<<" = v"<<i.uses[0]<<" < 0;\n";
      else if(i.opcode=="br.nonzero") o<<"  if(v"<<i.uses[0]<<") goto b"<<i.block_targets[0]<<"; else goto b"<<i.block_targets[1]<<";\n";
      else if(i.opcode=="br") o<<"  goto b"<<i.block_targets[0]<<";\n";
      else o<<"  return v"<<i.uses[0]<<";\n";
    }
  }
  o<<"}\n"; return Result<std::string>::ok(o.str());
}
} // namespace limestone::machineir_native
