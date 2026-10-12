#include "vmweave.hpp"
#include "lua_source.hpp"
#include <kaguya/kaguya.hpp>
#include <fstream>
#include <sstream>
#include <cmath>

namespace limestone::vmweave {
namespace {
template<class T> T get(const kaguya::LuaTable& t, const char* key, T fallback) {
  kaguya::LuaRef v=t[key];
  if(v.isNilref()) return fallback;
  if(!v.isType<T>()) throw std::runtime_error(std::string("invalid type for '")+key+"'");
  return v.get<T>();
}
void keys(const kaguya::LuaTable& t, std::initializer_list<std::string_view> allowed) {
  for(const auto& key:t.keys<std::string>())
    if(std::find(allowed.begin(),allowed.end(),key)==allowed.end()) throw std::runtime_error("unknown attribute '"+key+"'");
}
template<class F> void array(const kaguya::LuaTable& t, F f) {
  auto ks=t.keys<int>();
  std::sort(ks.begin(),ks.end());
  int next=1;
  for(int k:ks) {if(k!=next++) throw std::runtime_error("expected a dense sequence"); f(t[k]);}
}
uint32_t integer(const kaguya::LuaTable& t,const char* key,uint32_t fallback,uint32_t max=1048576) {
  kaguya::LuaRef v=t[key]; if(v.isNilref()) return fallback;
  double n=v.get<double>();
  if(!v.isType<double>() || !std::isfinite(n) || n<0 || n>max || n!=static_cast<uint32_t>(n)) throw std::runtime_error(std::string("invalid integer '")+key+"'");
  return static_cast<uint32_t>(n);
}
Location source(const kaguya::LuaTable& t, const Location& fallback) {
  kaguya::LuaRef v=t["source"]; if(v.isNilref()) return fallback;
  auto l=v.get<kaguya::LuaTable>(); keys(l,{"file","line"});
  auto result=Location{get<std::string>(l,"file",fallback.file),integer(l,"line",fallback.line,2147483647)};
  if(result.file=="<lua>") return fallback;
  return result;
}
Specification convert(const kaguya::LuaTable& t, const std::string& file) {
  keys(t,{"name","execution","stack","fields","instructions","components","hooks","memory","subsystems","rewrites","frame_capacity","ipc_capacity","source"});
  Specification s; s.source=source(t,{file,1}); s.name=t["name"].get<std::string>();
  s.execution=get<std::string>(t,"execution","switch"); s.hooks=get<bool>(t,"hooks",false);
  s.frame_capacity=integer(t,"frame_capacity",64); s.ipc_capacity=integer(t,"ipc_capacity",64);
  kaguya::LuaRef stack=t["stack"];
  if(!stack.isNilref()) {auto a=stack.get<kaguya::LuaTable>(); keys(a,{"cell","capacity"}); s.cell=get<std::string>(a,"cell","intptr_t"); s.stack_capacity=integer(a,"capacity",1024);}
  kaguya::LuaRef memory=t["memory"];
  if(!memory.isNilref()) {auto a=memory.get<kaguya::LuaTable>(); keys(a,{"allocator"}); s.allocator=get<std::string>(a,"allocator","custom");}
  kaguya::LuaRef fields=t["fields"];
  if(!fields.isNilref()) array(fields.get<kaguya::LuaTable>(),[&](kaguya::LuaRef value){
    auto f=value.get<kaguya::LuaTable>(); keys(f,{"name","type","count"});
    uint32_t count=integer(f,"count",0); if(!f["count"].isNilref() && !count) throw std::runtime_error("array count must be positive");
    s.fields.push_back({f["name"].get<std::string>(),f["type"].get<std::string>(),count});
  });
  kaguya::LuaRef instructions=t["instructions"];
  if(!instructions.isNilref()) array(instructions.get<kaguya::LuaTable>(),[&](kaguya::LuaRef value){
    auto a=value.get<kaguya::LuaTable>(); keys(a,{"name","opcode","body","semantics","stack_effect","operands","flow","source"});
    Instruction i; i.name=a["name"].get<std::string>(); i.source=source(a,s.source);
    if(!a["body"].isNilref() && !a["semantics"].isNilref()) throw std::runtime_error("body and semantics are aliases");
    i.semantics=get<std::string>(a,"semantics",get<std::string>(a,"body",""));
    i.stack_effect=get<std::string>(a,"stack_effect","( -- )"); i.flow=get<std::string>(a,"flow","next");
    if(!a["opcode"].isNilref()) i.opcode=integer(a,"opcode",0,2147483647);
    if(!a["operands"].isNilref()) array(a["operands"].get<kaguya::LuaTable>(),[&](kaguya::LuaRef o){i.operands.push_back(o.get<std::string>());});
    s.instructions.push_back(std::move(i));
  });
  if(t["components"].isNilref()) s.components={"token","opcode","insncode","dispatch","tape","compile"};
  else array(t["components"].get<kaguya::LuaTable>(),[&](kaguya::LuaRef c){s.components.push_back(c.get<std::string>());});
  if(!t["subsystems"].isNilref()) {
    auto a=t["subsystems"].get<kaguya::LuaTable>();
    for(const auto& key:a.keys<std::string>()) s.subsystem_c[key]=get<std::string>(a,key.c_str(),"");
  }
  if(!t["rewrites"].isNilref()) array(t["rewrites"].get<kaguya::LuaTable>(),[&](kaguya::LuaRef r){
    auto a=r.get<kaguya::LuaTable>(); keys(a,{"from","to","equivalent"});
    if(!get<bool>(a,"equivalent",false)) throw std::runtime_error("rewrite requires explicit equivalent=true assertion");
    Rewrite rule;
    array(a["from"].get<kaguya::LuaTable>(),[&](kaguya::LuaRef n){rule.from.push_back(n.get<std::string>());});
    array(a["to"].get<kaguya::LuaTable>(),[&](kaguya::LuaRef n){rule.to.push_back(n.get<std::string>());});
    s.rewrites.push_back(std::move(rule));
  });
  return s;
}
}
Result<Specification> load_lua(std::string_view text,std::string_view file) {
  if(text.size()>4*1024*1024 || text.find('\0')!=std::string_view::npos)
    return Result<Specification>::err({Error::Code::Parse,"VMWeave: invalid/oversized Lua source"});
  std::string diagnostic;
  try {
    kaguya::State state;
    state.setErrorHandler([&](int,const char* message){diagnostic=message?message:"Lua error";});
    state["__vmweave_emit"]=kaguya::function([&](kaguya::LuaTable description) {
      auto lowered=lower(convert(description,std::string(file)));
      if(!lowered) throw std::runtime_error(lowered.error().message);
      auto result=emit_c(lowered.value()); if(!result) throw std::runtime_error(result.error().message);
      return result.value();
    });
    std::istringstream module_source(vmweave_lua_source);
    kaguya::LuaRef module=state.loadstream(module_source,"@vmweave/vmweave.lua").call<kaguya::LuaRef>();
    state["package"]["loaded"]["vmweave"]=module;
    std::istringstream input{std::string(text)};
    auto chunk=state.loadstream(input,("@"+std::string(file)).c_str());
    if(chunk.isNilref()) throw std::runtime_error(diagnostic);
    kaguya::LuaRef returned=chunk.call<kaguya::LuaRef>();
    if(!diagnostic.empty()) throw std::runtime_error(diagnostic);
    if(returned.isNilref()) returned=state["package"]["loaded"]["vmweave"]["last"];
    if(returned.isNilref()) throw std::runtime_error("specification must return a VM description");
    auto converted=convert(returned.get<kaguya::LuaTable>(),std::string(file));
    if(!diagnostic.empty()) throw std::runtime_error(diagnostic);
    return validate(std::move(converted));
  } catch(const std::exception& e) {
    return Result<Specification>::err({Error::Code::Parse,"VMWeave: "+std::string(file)+": "+e.what()});
  }
}
Result<Specification> load_file(const std::string& file) {
  std::ifstream in(file,std::ios::binary);
  if(!in) return Result<Specification>::err({Error::Code::NotFound,"VMWeave: cannot open '"+file+"'"});
  std::ostringstream text; text<<in.rdbuf(); return load_lua(text.str(),file);
}
} // namespace limestone::vmweave
