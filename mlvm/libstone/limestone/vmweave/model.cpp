#include "vmweave.hpp"
#include <cctype>
#include <set>
#include <sstream>

namespace limestone::vmweave {
const std::vector<std::string>& component_names() {
  static const std::vector<std::string> names = {"token", "opcode", "insncode", "srtbl", "addrtbl", "insntbl", "dispatch", "ipc", "rewrite", "compile", "memory", "module", "jit", "tape", "frame", "optim", "atomic", "object"};
  return names;
}
static bool identifier(const std::string& s) {
  static const std::set<std::string> reserved = {
    "auto","break","case","char","const","continue","default","do","double","else","enum","extern",
    "float","for","goto","if","inline","int","long","register","restrict","return","short","signed",
    "sizeof","static","struct","switch","typedef","union","unsigned","void","volatile","while",
    "alignas","alignof","bool","class","namespace","new","delete","template","this","true","false",
    "nullptr","operator","private","protected","public","virtual"};
  if(s.empty() || !std::isalpha(static_cast<unsigned char>(s[0])) || reserved.contains(s)) return false;
  return std::all_of(s.begin(),s.end(),[](unsigned char c){return c<128 && (std::isalnum(c)||c=='_');});
}
static bool effect(const std::string& text) {
  if(text.find('\0')!=std::string::npos) return false;
  std::istringstream in(text); std::string token;
  if(!(in>>token) || token!="(") return false;
  bool separator=false, closed=false;
  while(in>>token) {
    if(token=="--") {if(separator || closed) return false; separator=true;}
    else if(token==")") {closed=true; break;}
    else if(token.find_first_of("();\n\r")!=std::string::npos) return false;
  }
  return separator && closed && !(in>>token);
}
static bool has(const Specification& s, std::string_view c) {
  return std::find(s.components.begin(),s.components.end(),c)!=s.components.end();
}
Result<Specification> validate(Specification s) {
  auto error=[&](std::string message, Error::Code code=Error::Code::InvalidArgument) {
    return Result<Specification>::err({code,"VMWeave: "+s.source.file+":"+std::to_string(s.source.line)+": VM '"+s.name+"': "+message});
  };
  if(!identifier(s.name)) return error("invalid VM name");
  if(s.source.file.find('\0')!=std::string::npos || s.source.line>2147483647) return error("invalid source location");
  if(s.execution!="none" && s.execution!="switch" && s.execution!="subroutine" && s.execution!="indirect" && s.execution!="direct") return error("unknown execution model '"+s.execution+"'");
  if(s.cell!="intptr_t" && s.cell!="int64_t" && s.cell!="uint64_t") return error("cell must be intptr_t, int64_t, or uint64_t",Error::Code::Unsupported);
  if(!s.stack_capacity || s.stack_capacity>1048576 || !s.frame_capacity || s.frame_capacity>1048576 || !s.ipc_capacity || s.ipc_capacity>1048576) return error("capacity must be in 1..1048576");
  if(s.hooks && s.execution=="none") return error("hooks require dispatch");
  if(s.allocator!="custom" && s.allocator!="system" && s.allocator!="kalloc" && s.allocator!="jemalloc" && s.allocator!="memtkx" && s.allocator!="mimalloc") return error("unknown allocator");
  // Non-system allocators use exactly the same callback ABI. The embedding
  // application owns their arenas and dependency-specific initialization.
  std::set<std::string> components;
  for(const auto& c:s.components) {
    if(std::find(component_names().begin(),component_names().end(),c)==component_names().end()) return error("unknown component '"+c+"'");
    if(!components.insert(c).second) return error("duplicate component '"+c+"'");
  }
  for(const auto& [c,body]:s.subsystem_c) {
    if(!components.contains(c)) return error("C subsystem '"+c+"' is not selected");
    if(body.find('\0')!=std::string::npos) return error("NUL in subsystem C source");
  }
  if(has(s,"object") && !has(s,"memory")) return error("object requires memory");
  if(has(s,"compile") && !has(s,"tape")) return error("compile requires tape");
  if(has(s,"jit") && !has(s,"tape")) return error("jit requires tape");
  for(const auto& [table,mode]:std::vector<std::pair<std::string,std::string>>{{"srtbl","subroutine"},{"addrtbl","indirect"},{"insntbl","direct"}})
    if(has(s,table) && s.execution!=mode) return error(table+" requires "+mode+" execution");
  if(has(s,"dispatch") && s.execution=="none") return error("dispatch requires an execution model");
  if(has(s,"dispatch") && s.execution=="subroutine" && !has(s,"srtbl")) return error("subroutine dispatch requires srtbl");
  if(has(s,"dispatch") && s.execution=="indirect" && !has(s,"addrtbl")) return error("indirect dispatch requires addrtbl");
  if(has(s,"dispatch") && s.execution=="direct" && !has(s,"insntbl")) return error("direct dispatch requires insntbl");
  static const std::set<std::string> field_types={"u8","u16","u32","u64","i8","i16","i32","i64","f32","f64","ptr","size"};
  std::set<std::string> fields, names;
  for(const auto& f:s.fields) {
    if(!identifier(f.name) || f.name.starts_with("vmweave_") || f.name.starts_with("vw_")) return error("invalid/reserved state name '"+f.name+"'");
    if(!fields.insert(f.name).second) return error("duplicate state '"+f.name+"'");
    if(!field_types.contains(f.type) || f.count>1048576) return error("invalid state type/count for '"+f.name+"'");
  }
  static const std::set<std::string> handler_reserved={"init","step","run","run_direct","thread","compile","destroy","lookup","push","pop","jump","halt","call","ret","alloc","free","token","rewrite",
    "vm","cell","handler","instruction","tape","metadata","threaded","allocator","activation","object","symbol","module","atomic","cache_entry","jit_backend","nil","integer","floating","boolean","reference"};
  std::set<uint32_t> opcodes;
  for(const auto& i:s.instructions) {
    auto prefix="instruction '"+i.name+"' ("+i.source.file+":"+std::to_string(i.source.line)+"): ";
    if(i.source.file.find('\0')!=std::string::npos || i.source.line>2147483647) return error(prefix+"invalid source location");
    if(!identifier(i.name) || handler_reserved.contains(i.name) || i.name.starts_with("vw_") || i.name.starts_with("opcode_") || i.name.starts_with("memory_") || i.name.starts_with("object_") || i.name.starts_with("atomic_") || i.name.starts_with("ipc_") || i.name.starts_with("module_") || i.name.starts_with("optim_") || i.name.starts_with("jit_") || i.name.starts_with("tape_")) return error(prefix+"invalid/reserved name");
    if(!names.insert(i.name).second) return error(prefix+"duplicate mnemonic",Error::Code::Conflict);
    if(i.opcode && (*i.opcode>2147483647 || !opcodes.insert(*i.opcode).second)) return error(prefix+"duplicate or invalid opcode",Error::Code::Conflict);
    if(!effect(i.stack_effect)) return error(prefix+"expected stack effect '( before -- after )'");
    if(i.flow!="next" && i.flow!="jump" && i.flow!="branch" && i.flow!="call" && i.flow!="return" && i.flow!="halt") return error(prefix+"unknown control flow");
    if((i.flow=="call" || i.flow=="return") && !has(s,"frame")) return error(prefix+"call/return requires frame");
    if(i.operands.size()>16) return error(prefix+"at most 16 operands supported");
    for(const auto& operand:i.operands) if(operand!="cell" && operand!="label") return error(prefix+"unsupported operand type '"+operand+"'");
    if(i.semantics.find('\0')!=std::string::npos) return error(prefix+"NUL in C semantics");
  }
  for(const auto& r:s.rewrites) {
    if(r.from.empty() || r.from.size()>32 || r.to.size()>=r.from.size()) return error("rewrite must strictly shorten a nonempty sequence");
    for(const auto* seq:{&r.from,&r.to}) for(const auto& name:*seq) {
      auto i=std::find_if(s.instructions.begin(),s.instructions.end(),[&](const auto& x){return x.name==name;});
      if(i==s.instructions.end() || !i->operands.empty() || i->flow!="next") return error("rewrite requires known operand-free straight-line instructions");
    }
    auto profile=[&](const std::vector<std::string>& sequence) {
      int64_t depth=0,required=0,peak=0;
      for(const auto& name:sequence) {
        const auto& i=*std::find_if(s.instructions.begin(),s.instructions.end(),[&](const auto& x){return x.name==name;});
        std::istringstream text(i.stack_effect); std::string token; int64_t inputs=0,outputs=0; bool after=false;
        while(text>>token) {if(token=="--") after=true; else if(token!="(" && token!=")") ++(after?outputs:inputs);}
        required=std::max(required,inputs-depth); depth+=outputs-inputs; peak=std::max(peak,depth);
      }
      return std::tuple{depth,required,peak};
    };
    auto [from_delta,from_required,from_peak]=profile(r.from);
    auto [to_delta,to_required,to_peak]=profile(r.to);
    if(from_delta!=to_delta || to_required>from_required || to_peak>from_peak) return error("rewrite has incompatible stack effects");
  }
  if(!s.rewrites.empty() && !has(s,"rewrite")) return error("rewrite rules require rewrite component");
  std::sort(s.fields.begin(),s.fields.end(),[](const auto& a,const auto& b){return a.name<b.name;});
  std::sort(s.instructions.begin(),s.instructions.end(),[](const auto& a,const auto& b){return a.name<b.name;});
  std::sort(s.components.begin(),s.components.end());
  return Result<Specification>::ok(std::move(s));
}
Result<Module> lower(Specification spec) {
  auto checked=validate(std::move(spec)); if(!checked) return Result<Module>::err(checked.error());
  Module m; m.configuration=std::move(checked.value());
  std::set<uint32_t> used;
  for(const auto& i:m.configuration.instructions) if(i.opcode) used.insert(*i.opcode);
  uint32_t next=0;
  for(auto& i:m.configuration.instructions) {
    if(!i.opcode) {while(used.contains(next)) ++next; i.opcode=next; used.insert(next);}
    m.words.push_back({i.name,i.stack_effect,i.flow,i.semantics,*i.opcode,i.operands,i.source});
  }
  m.configuration.instructions.clear();
  return Result<Module>::ok(std::move(m));
}
} // namespace limestone::vmweave
