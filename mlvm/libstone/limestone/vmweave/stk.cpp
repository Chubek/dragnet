#include "internal.hpp"
#include <set>

namespace limestone::vmweave {
Result<std::string> print_stk(const Module& input) {
  auto checked=checked_module(input); if(!checked) return Result<std::string>::err(checked.error());
  const auto& m=checked.value(); const auto& s=m.configuration;
  std::ostringstream o;
  auto q=[](const std::string& x){return std::quoted(x);};
  auto body=[&](const std::string& c){o<<"c{ "<<c.size()<<'\n'<<c<<"\n}c";};
  o<<"stk-00 1\nvm "<<q(s.name)<<"\nconfiguration "<<q(s.execution)<<' '<<q(s.cell)<<' '<<s.stack_capacity<<' '<<s.frame_capacity<<' '<<s.ipc_capacity<<' '<<q(s.allocator)<<' '<<(s.hooks?1:0)<<"\nsource "<<q(s.source.file)<<' '<<s.source.line<<'\n';
  for(const auto& c:s.components) o<<"component "<<q(c)<<'\n';
  for(const auto& f:s.fields) o<<"field "<<q(f.name)<<' '<<q(f.type)<<' '<<f.count<<'\n';
  for(const auto& [c,text]:s.subsystem_c) {o<<"subsystem "<<q(c)<<' '; body(text); o<<'\n';}
  for(const auto& w:m.words) {
    o<<": "<<q(w.name)<<" opcode "<<w.opcode<<" operands "<<w.operands.size();
    for(const auto& operand:w.operands) o<<' '<<q(operand);
    o<<" effect "<<q(w.effect)<<" flow "<<q(w.flow)<<" source "<<q(w.source.file)<<' '<<w.source.line<<'\n';
    body(w.c_body); o<<" ;\n";
  }
  for(const auto& r:s.rewrites) {
    o<<"rewrite "<<r.from.size(); for(const auto& n:r.from) o<<' '<<q(n);
    o<<" to "<<r.to.size(); for(const auto& n:r.to) o<<' '<<q(n); o<<'\n';
  }
  o<<"end\n"; return Result<std::string>::ok(o.str());
}
Result<Module> parse_stk(std::string_view text,std::string_view file) {
  if(text.size()>4*1024*1024 || text.find('\0')!=std::string_view::npos) return Result<Module>::err({Error::Code::Parse,"VMWeave: invalid/oversized STK-00 source"});
  std::istringstream in{std::string(text)};
  try {
    auto token=[&]() {std::string x; if(!(in>>x)) throw std::runtime_error("unexpected end of STK-00"); return x;};
    auto expect=[&](const std::string& x) {if(token()!=x) throw std::runtime_error("expected '"+x+"'");};
    auto number=[&](uint32_t max=2147483647) {
      auto x=token(); size_t end=0; auto value=std::stoull(x,&end);
      if(x.empty() || x[0]=='-' || end!=x.size() || value>max) throw std::runtime_error("invalid integer");
      return static_cast<uint32_t>(value);
    };
    auto string=[&]() {
      in>>std::ws; if(in.peek()!='"') throw std::runtime_error("expected quoted string");
      std::string x; if(!(in>>std::quoted(x))) throw std::runtime_error("unterminated quoted string"); return x;
    };
    auto body=[&]() {
      expect("c{"); auto size=number(4*1024*1024);
      if(in.get()!='\n') throw std::runtime_error("expected newline after C byte count");
      std::string c(size,'\0'); if(!in.read(c.data(),size)) throw std::runtime_error("truncated C primitive");
      if(in.get()!='\n') throw std::runtime_error("expected newline after C primitive");
      expect("}c"); return c;
    };
    expect("stk-00"); if(number()!=1) throw std::runtime_error("unsupported STK-00 version");
    Specification s;
    expect("vm"); s.name=string(); expect("configuration"); s.execution=string(); s.cell=string();
    s.stack_capacity=number(1048576); s.frame_capacity=number(1048576); s.ipc_capacity=number(1048576); s.allocator=string(); s.hooks=number(1)!=0;
    expect("source"); s.source.file=string(); s.source.line=number();
    for(;;) {
      auto word=token();
      if(word=="end") break;
      if(word=="component") s.components.push_back(string());
      else if(word=="field") {auto name=string(),type=string(); s.fields.push_back({name,type,number(1048576)});}
      else if(word=="subsystem") {auto c=string(); if(s.subsystem_c.contains(c)) throw std::runtime_error("duplicate subsystem"); s.subsystem_c[c]=body();}
      else if(word==":") {
        Instruction i; i.name=string(); expect("opcode"); i.opcode=number(); expect("operands"); auto count=number(16);
        for(uint32_t n=0;n<count;++n) i.operands.push_back(string());
        expect("effect"); i.stack_effect=string(); expect("flow"); i.flow=string(); expect("source"); i.source.file=string(); i.source.line=number();
        i.semantics=body(); expect(";"); s.instructions.push_back(std::move(i));
      } else if(word=="rewrite") {
        Rewrite r; auto count=number(32); for(uint32_t n=0;n<count;++n) r.from.push_back(string());
        expect("to"); count=number(31); for(uint32_t n=0;n<count;++n) r.to.push_back(string()); s.rewrites.push_back(std::move(r));
      } else throw std::runtime_error("unknown STK-00 declaration '"+word+"'");
    }
    in>>std::ws; if(in.peek()!=EOF) throw std::runtime_error("trailing STK-00 input");
    return lower(std::move(s));
  } catch(const std::exception& e) {
    auto position=in.tellg(); size_t offset=position<0?text.size():static_cast<size_t>(position);
    auto line=1+std::count(text.begin(),text.begin()+std::min(offset,text.size()),'\n');
    return Result<Module>::err({Error::Code::Parse,"VMWeave: "+std::string(file)+":"+std::to_string(line)+": "+e.what()});
  }
}
} // namespace limestone::vmweave
