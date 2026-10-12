#include "metacode.hpp"
#include "json.hpp"
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <dparse.h>

extern "C" D_ParserTables parser_tables_isa;

namespace limestone::metacode {
namespace {
std::string quote(std::string_view text) {
  constexpr char hex[]="0123456789abcdef";
  std::string out="\"";
  for (unsigned char c : text) {
    switch (c) {
      case '\\': out+="\\\\"; break;
      case '"': out+="\\\""; break;
      case '\n': out+="\\n"; break;
      case '\r': out+="\\r"; break;
      case '\t': out+="\\t"; break;
      default: if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);
    }
  }
  return out+'"';
}
std::string_view trim(std::string_view s) {
  const auto b=s.find_first_not_of(" \t\r\n");
  if (b==s.npos) return {};
  return s.substr(b,s.find_last_not_of(" \t\r\n")-b+1);
}
struct Reader {
  std::string_view input, file;
  std::vector<size_t> lines{0};
  Reader(std::string_view text,std::string_view filename):input(text),file(filename) {
    for(size_t i=0;i<input.size();++i)if(input[i]=='\n')lines.push_back(i+1);
  }
  std::string_view symbol(D_ParseNode* n) const { return parser_tables_isa.symbols[n->symbol].name; }
  std::string_view text(D_ParseNode* n) const { return trim({n->start_loc.s,static_cast<size_t>(n->end-n->start_loc.s)}); }
  SourceLocation source(D_ParseNode* n) const {
    SourceLocation l{std::string(file),static_cast<size_t>(n->start_loc.s-input.data()),1,1};
    auto it=std::upper_bound(lines.begin(),lines.end(),l.offset);auto index=static_cast<size_t>(it-lines.begin()-1);
    l.line=static_cast<uint32_t>(index+1);l.column=static_cast<uint32_t>(l.offset-lines[index]+1);
    return l;
  }
  std::vector<D_ParseNode*> collect(D_ParseNode* n, std::string_view name) const {
    if (symbol(n)==name) return {n};
    std::vector<D_ParseNode*> out;
    for(int i=0;i<d_get_number_of_children(n);++i) {
      auto part=collect(d_get_child(n,i),name);out.insert(out.end(),part.begin(),part.end());
    }
    return out;
  }
  D_ParseNode* child(D_ParseNode* n,std::string_view name) const {
    for(int i=0;i<d_get_number_of_children(n);++i) {
      auto c=d_get_child(n,i);if(symbol(c)==name)return c;
      auto kind=parser_tables_isa.symbols[c->symbol].kind;
      if(kind==D_SYMBOL_INTERNAL||kind==D_SYMBOL_EBNF)if(auto found=child(c,name))return found;
    }
    return nullptr;
  }
  [[noreturn]] void fail(D_ParseNode* n, std::string message) const {
    const auto l=source(n);
    throw Error{Error::Code::Parse,l.file+":"+std::to_string(l.line)+":"+std::to_string(l.column)+": "+message};
  }
  Value value(D_ParseNode* n) const {
    const auto s=text(n);
    Value v;
    if (!s.empty() && s.front()=='"') {
      auto parsed=parse_json(s,file);
      if(!parsed)fail(n,"invalid quoted metadata string: "+parsed.error().message);
      v=std::move(parsed.value());
    }
    else if (s=="true" || s=="false") v=Value(s=="true");
    else if (!s.empty() && s.front()=='{') {
      auto fs=collect(n,"Fields");v=Value(fields(fs.front()));
    } else if (!s.empty() && s.front()=='[') {
      Value::Array a;
      // Skip the outer Value to collect only immediate array elements.
      auto array=child(n,"Array");
      for(auto x:collect(array,"ArrayValue"))a.push_back(value(x));
      v=Value(std::move(a));
    } else {
      int64_t number=0;
      const auto [end,err]=std::from_chars(s.data(),s.data()+s.size(),number);
      if(err==std::errc{} && end==s.data()+s.size())v=Value(number);
      else if(s.starts_with("0x")) {
        uint64_t hex=0;auto [p,e]=std::from_chars(s.data()+2,s.data()+s.size(),hex,16);
        if(e!=std::errc{} || p!=s.data()+s.size())fail(n,"invalid hexadecimal integer");
        v=Value(hex);
      } else if(err==std::errc::result_out_of_range && s.find_first_not_of("-0123456789")==s.npos) {
        uint64_t natural=0;auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),natural);
        if(error!=std::errc{}||end!=s.data()+s.size())fail(n,"integer out of range");
        v=Value(natural);
      }
      else if(auto list=child(n,"BareList");list && s.find(',')!=s.npos) {
        Value::Array a;for(auto x:collect(list,"BareAtom"))a.emplace_back(std::string(text(x)));v=Value(std::move(a));
      } else v=Value(std::string(s));
    }
    v.source=source(n);return v;
  }
  Value::Object fields(D_ParseNode* n) const {
    Value::Object fs;
    for(auto f:collect(n,"Field")) {
      const auto key=std::string(text(child(f,"FieldName")));
      auto v=child(f,"Value")?value(child(f,"Value")):child(f,"Object")?value(child(f,"Object")):child(f,"Fields")?Value(fields(child(f,"Fields"))):Value(std::string{});
      v.source=source(f);
      if(!fs.emplace(key,std::move(v)).second)fail(f,"duplicate field: "+key);
    }
    return fs;
  }
  uint32_t u32(D_ParseNode* n) const {
    auto v=value(n);uint64_t x=0;
    if(auto p=std::get_if<int64_t>(&v.data)){if(*p<0)fail(n,"negative register number/width");x=*p;}
    else if(auto p=std::get_if<uint64_t>(&v.data))x=*p;
    else fail(n,"invalid register number/width");
    if(x>std::numeric_limits<uint32_t>::max())fail(n,"register number/width out of range");
    return static_cast<uint32_t>(x);
  }
};
void silent_error(D_Parser*) {}
D_ParseNode* ambiguity(D_Parser*,int,D_ParseNode** nodes) { return nodes[0]; }
}

std::string Value::text() const {
  if(auto p=std::get_if<std::string>(&data))return *p;
  if(auto p=std::get_if<int64_t>(&data))return std::to_string(*p);
  if(auto p=std::get_if<uint64_t>(&data))return std::to_string(*p);
  if(auto p=std::get_if<bool>(&data))return *p?"true":"false";
  if(auto p=std::get_if<Array>(&data)) {
    std::string s="[";for(size_t i=0;i<p->size();++i){if(i)s+=",";s+=std::holds_alternative<std::string>((*p)[i].data)?quote((*p)[i].text()):(*p)[i].text();}return s+"]";
  }
  const auto& o=std::get<Object>(data);std::vector<std::string> keys;
  for(auto&[k,v]:o)keys.push_back(k);std::sort(keys.begin(),keys.end());
  std::string s="{";for(auto&k:keys){s+=k+"=";auto&v=o.at(k);s+=std::holds_alternative<std::string>(v.data)?quote(v.text()):v.text();s+=";";}return s+"}";
}

Result<Architecture> parse_isa(std::string_view input,std::string_view file) {
  if(input.size()>static_cast<size_t>(std::numeric_limits<int>::max()))return Result<Architecture>::err({Error::Code::InvalidArgument,"ISA input too large"});
  if(auto offset=input.find('\0');offset!=input.npos)return Result<Architecture>::err({Error::Code::Parse,std::string(file)+": embedded NUL at byte "+std::to_string(offset)});
  size_t depth=0;bool quoted=false,escape=false,comment=false;
  for(char c:input) {
    if(comment){if(c=='\n')comment=false;continue;}
    if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}
    if(c=='#'){comment=true;continue;}if(c=='"'){quoted=true;continue;}
    if((c=='{'||c=='['||c=='(')&&++depth>256)return Result<Architecture>::err({Error::Code::ResourceLimit,"ISA nesting limit exceeded"});
    if((c=='}'||c==']'||c==')')&&depth)--depth;
  }
  std::string buffer(input);
  std::unique_ptr<D_Parser,decltype(&free_D_Parser)> parser(new_D_Parser(&parser_tables_isa,0),free_D_Parser);
  parser->save_parse_tree=1;parser->fixup_EBNF_productions=1;parser->error_recovery=0;parser->syntax_error_fn=silent_error;parser->ambiguity_fn=ambiguity;
  auto tree=dparse(parser.get(),buffer.data(),static_cast<int>(buffer.size()));
  if(!tree || parser->syntax_errors) {
    if(tree)free_D_ParseNode(parser.get(),tree);
    return Result<Architecture>::err({Error::Code::Parse,std::string(file)+":"+std::to_string(parser->loc.line)+":"+std::to_string(parser->loc.col+1)+": invalid ISA syntax"});
  }
  auto deleter=[&](D_ParseNode* p){free_D_ParseNode(parser.get(),p);};
  std::unique_ptr<D_ParseNode,decltype(deleter)> owned(tree,deleter);
  Reader r{buffer,file};Architecture a;std::unordered_set<std::string> classes,ops;
  try {
    for(auto t:r.collect(tree,"Top")) {
      auto n=d_get_child(t,0);const auto kind=r.symbol(n);
      auto name=r.child(n,"Ident");auto fs=r.child(n,"Fields");
      if(kind=="Arch") {
        if(!a.name.empty())r.fail(n,"multiple architectures");
        a.name=r.text(name);a.source=r.source(n);
        auto fields=r.fields(fs);
        std::vector<std::string> keys;for(auto& [key,value]:fields)keys.push_back(key);std::sort(keys.begin(),keys.end());
        for(auto& key:keys)if(!a.fields.emplace(key,std::move(fields.at(key))).second)r.fail(n,"duplicate section: "+key);
      } else if(kind=="Regclass") {
        auto klass=std::string(r.text(name));if(!classes.insert(klass).second)r.fail(n,"duplicate register class");
        std::unordered_set<std::string> names;
        for(auto e:r.collect(n,"RegisterEntry")) {
          auto rn=std::string(r.text(r.child(e,"Ident")));auto nums=r.collect(e,"Number");
          if(!names.insert(rn).second)r.fail(e,"duplicate register: "+rn);
          auto width=r.u32(nums[0]);if(!width)r.fail(e,"zero register width");
          a.registers.push_back({rn,klass,width,r.u32(nums[1])});
        }
      } else if(kind=="Op") {
        Operation op;op.name=r.text(name);op.fields=r.fields(fs);op.source=r.source(n);
        if(!ops.insert(op.name).second)r.fail(n,"duplicate operation: "+op.name);
        if(auto it=op.fields.find("semantics");it!=op.fields.end())op.semantics=it->second.text();
        a.operations.push_back(std::move(op));
      } else if(kind=="Encoding") {
        if(!a.encodings.emplace(std::string(r.text(name)),r.fields(fs)).second)r.fail(n,"duplicate encoding");
      } else if(kind=="Alias") {
        if(!a.aliases.emplace(std::string(r.text(name)),r.value(r.child(n,"Value")).text()).second)r.fail(n,"duplicate alias");
      } else {
        auto key=std::string(r.text(d_get_child(n,0)));
        if(!a.fields.emplace(key,Value(r.fields(fs))).second)r.fail(n,"duplicate section: "+key);
      }
    }
    if(a.name.empty())return Result<Architecture>::err({Error::Code::Parse,"ISA contains no arch declaration"});
    auto profile=a.fields.find("profile");
    const auto* identity=profile==a.fields.end()?&a.fields:std::get_if<Value::Object>(&profile->second.data);
    if(!identity)return Result<Architecture>::err({Error::Code::Parse,"profile must be an object"});
    for(auto [key,dest]:{std::pair{"family",&a.family},{"model",&a.model},{"version",&a.version}})
      if(auto it=identity->find(key);it!=identity->end())*dest=it->second.text();
    return Result<Architecture>::ok(std::move(a));
  }catch(const Error& e){return Result<Architecture>::err(e);}
}
Result<Architecture> load_isa_file(const std::string& path) {
  std::ifstream f(path);if(!f)return Result<Architecture>::err({Error::Code::NotFound,"cannot open "+path});
  std::stringstream s;s<<f.rdbuf();return parse_isa(s.str(),path);
}
std::string dump(const Architecture& a) {
  return "arch "+a.name+" { family="+quote(a.family)+"; model="+quote(a.model)+"; version="+quote(a.version)+"; }\nregisters: "+std::to_string(a.registers.size())+"\noperations: "+std::to_string(a.operations.size())+"\n";
}
}
