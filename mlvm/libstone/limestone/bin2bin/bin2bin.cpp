#include "bin2bin.hpp"
#include "codec_internal.hpp"
#include <SExprTk.hpp>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#ifdef LIMESTONE_HAS_LMDB
#include <lmdb++.h>
#endif

namespace limestone::bin2bin {
struct CacheStorage {
#ifdef LIMESTONE_HAS_LMDB
  lmdb::env environment=lmdb::env::create();
#endif
};
namespace {
void field(std::string& out,std::string_view value) {
  out+=std::to_string(value.size())+":";out.append(value);
}
std::string identity(const Architecture& a) {
  std::string out;
  for(auto& s:{a.name,a.version,a.description,a.infobank_version,a.execution_domain,a.state_model})field(out,s);
  std::map<uint8_t,std::string> names(a.opcodes.begin(),a.opcodes.end());
  field(out,std::to_string(names.size()));
  for(auto&[opcode,name]:names){field(out,std::to_string(opcode));field(out,name);}
  field(out,"semantics");
  std::map<uint8_t,std::string> semantics(a.semantics.begin(),a.semantics.end());
  field(out,std::to_string(semantics.size()));
  for(auto&[opcode,term]:semantics){field(out,std::to_string(opcode));field(out,term);}
  field(out,"status");
  std::map<uint8_t,Status> statuses(a.status.begin(),a.status.end());
  field(out,std::to_string(statuses.size()));
  for(auto [opcode,status]:statuses){field(out,std::to_string(opcode));field(out,std::to_string(static_cast<int>(status)));}
  field(out,"control");std::map<uint8_t,ControlFlow> controls(a.control.begin(),a.control.end());field(out,std::to_string(controls.size()));for(auto [opcode,flow]:controls){field(out,std::to_string(opcode));field(out,std::to_string(static_cast<int>(flow)));}
  field(out,a.endianness);
  field(out,a.codec?"native":"builtin");if(a.codec){field(out,a.codec->identity);field(out,std::to_string(a.codec->max_instruction_bytes));}
  auto forms=a.forms;std::sort(forms.begin(),forms.end(),[](auto& x,auto& y){return x.id<y.id;});
  field(out,std::to_string(forms.size()));
  for(auto& f:forms){for(auto& s:{std::to_string(f.id),f.mnemonic,std::to_string(f.width),std::to_string(f.mask),std::to_string(f.base),f.semantics,std::to_string(static_cast<int>(f.status)),std::to_string(static_cast<int>(f.control)),f.target_operand,std::to_string(f.fields.size())})field(out,s);for(auto& p:f.fields)for(auto& s:{p.name,std::to_string(p.lsb),std::to_string(p.width),std::to_string(static_cast<int>(p.kind)),p.register_class,std::to_string(p.scale),std::to_string(p.relative_to_end)})field(out,s);}
  field(out,std::to_string(a.registers.size()));
  for(auto& [klass,regs]:a.registers){field(out,klass);field(out,std::to_string(regs.size()));for(auto& [id,name]:regs){field(out,std::to_string(id));field(out,name);}}
  return out;
}
std::string cache_key(const Architecture& src,const Architecture& dst,std::span<const uint8_t> bytes,const TranslationOptions& options) {
   std::string key="bin2bin:8:translation-schema:1:";
  field(key,identity(src));field(key,identity(dst));
  for(auto& s:{options.rule_version,options.optimization_configuration,options.translator_configuration,options.plugin_versions,options.runtime_configuration})field(key,s);
  field(key,std::to_string(options.source_address));field(key,std::to_string(options.target_address));
  field(key,options.semantic_transform?"transform":"identity");if(options.semantic_transform)field(key,options.semantic_transform->identity);
  field(key,options.region_transform?"region-transform":"region-identity");if(options.region_transform){auto& t=*options.region_transform;for(auto& s:{t.identity,t.source_state_model,t.target_state_model,t.source_domain,t.target_domain})field(key,s);}
  field(key,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});
  return key;
}
// LMDB limits key length. The digest indexes an entry; its full identity is
// checked on retrieval, so a digest collision is a cache miss, never a wrong hit.
std::string digest(std::string_view key) {
  uint64_t a=14695981039346656037ull,b=1099511628211ull;
  for(unsigned char c:key){a=(a^c)*1099511628211ull;b=(b+c+1)*14029467366897019727ull;}
  std::string out;
  for(auto x:{a,b})for(unsigned i=0;i<8;++i)out+=static_cast<char>(x>>(8*i));
  return out;
}
Result<std::optional<std::vector<uint8_t>>> cached(TranslationCache& cache,const std::string& key) {
  if(auto it=cache.entries.find(key);it!=cache.entries.end())return Result<std::optional<std::vector<uint8_t>>>::ok(it->second);
#ifdef LIMESTONE_HAS_LMDB
  if(cache.storage)try {
    auto txn=lmdb::txn::begin(cache.storage->environment,nullptr,MDB_RDONLY);
    auto db=lmdb::dbi::open(txn,nullptr);auto hash=digest(key);
    lmdb::val k{hash.data(),hash.size()},v;
    if(db.get(txn,k,v)) {
      std::string_view stored{v.data<const char>(),v.size()};
      // Value: fixed-size identity digest, full identity length, identity, output.
      if(stored.size()<24)return Result<std::optional<std::vector<uint8_t>>>::err({Error::Code::Internal,"corrupt translation cache entry"});
      uint64_t size=0;for(unsigned i=0;i<8;++i)size|=uint64_t(static_cast<unsigned char>(stored[16+i]))<<(8*i);
      if(size>stored.size()-24||digest(stored.substr(24))!=stored.substr(0,16))return Result<std::optional<std::vector<uint8_t>>>::err({Error::Code::Internal,"corrupt translation cache entry"});
      if(stored.substr(24,size)==key) {
        auto bytes=stored.substr(24+size);std::vector<uint8_t> out(bytes.begin(),bytes.end());
        cache.entries[key]=out;return Result<std::optional<std::vector<uint8_t>>>::ok(std::move(out));
      }
    }
  }catch(const std::exception& e){return Result<std::optional<std::vector<uint8_t>>>::err({Error::Code::Internal,std::string("LMDB cache read: ")+e.what()});}
#endif
  return Result<std::optional<std::vector<uint8_t>>>::ok(std::nullopt);
}
Result<int> store(TranslationCache& cache,const std::string& key,const std::vector<uint8_t>& bytes) {
#ifdef LIMESTONE_HAS_LMDB
  if(cache.storage)try {
    std::string payload=key;payload.append(bytes.begin(),bytes.end());
    auto hash=digest(key);std::string value=digest(payload);
    for(unsigned i=0;i<8;++i)value+=static_cast<char>(uint64_t(key.size())>>(8*i));
    value+=payload;
    auto txn=lmdb::txn::begin(cache.storage->environment);auto db=lmdb::dbi::open(txn,nullptr);
    lmdb::val k{hash.data(),hash.size()},v{value.data(),value.size()};db.put(txn,k,v);txn.commit();
  }catch(const std::exception& e){return Result<int>::err({Error::Code::Internal,std::string("LMDB cache write: ")+e.what()});}
#endif
  cache.entries[key]=bytes;return Result<int>::ok(0);
}
std::string render(const sexprtk::Cell& c) {
  if(!c.tail.empty())throw Error{Error::Code::Unsupported,"quoted/dotted binary semantics require an adapter"};
  if(c.head.is_symbol())return c.head.as_string();
  if(c.head.is_int())return std::to_string(c.head.as_int());
  if(c.head.is_string()) {
    std::string out="\"";for(char ch:c.head.as_string()){if(ch=='\\'||ch=='\"')out+='\\';out+=ch;}return out+'"';
  }
  if(c.head.is_list()) {
    std::string out="(";bool first=true;
    const auto& list=c.head.as_list();for(size_t i=0;i<list.size();++i){if(!first)out+=' ';first=false;out+=render(list[i]);}return out+")";
  }
  throw Error{Error::Code::Unsupported,"unsupported binary semantic atom"};
}
Result<std::string> normalize(std::string_view text) {
  if(text.empty())return Result<std::string>::err({Error::Code::Unsupported,"instruction has no semantic contract"});
  size_t depth=0;bool quote=false,escape=false,comment=false;
  for(char c:text) {
    if(comment){if(c=='\n')comment=false;continue;}
    if(quote){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quote=false;continue;}
    if(c==';'){comment=true;continue;}if(c=='"'){quote=true;continue;}
    if(c=='('&&++depth>256)return Result<std::string>::err({Error::Code::ResourceLimit,"binary semantics nesting limit"});
    if(c==')'){if(!depth)return Result<std::string>::err({Error::Code::Parse,"unexpected ')' in binary semantics"});--depth;}
  }
  if(quote||depth)return Result<std::string>::err({Error::Code::Parse,"unterminated binary semantics"});
  auto parsed=sexprtk::SExprTk{}.parse(sexprtk::Source::from_string(std::string(text),"<binary-semantics>"));
  if(!parsed.ok()||parsed.root.size()!=1)return Result<std::string>::err({Error::Code::Parse,"expected one binary semantic expression"});
  try{return Result<std::string>::ok(render(parsed.root.front()));}catch(const Error& e){return Result<std::string>::err(e);}
}
Status status(const Architecture& a,uint8_t opcode) {
  auto it=a.status.find(opcode);return it==a.status.end()?Status::Supported:it->second;
}
const metacode::Value::Object* object(const metacode::Value::Object& fields,std::string_view name) {
  auto it=fields.find(std::string(name));return it==fields.end()?nullptr:std::get_if<metacode::Value::Object>(&it->second.data);
}
std::string text(const metacode::Value::Object& fields,std::string_view name) {
  auto it=fields.find(std::string(name));return it==fields.end()?"":it->second.text();
}
}
Result<int> open_cache(TranslationCache& cache,const std::string& path,size_t map_size) {
  if(path.empty()||map_size<1048576)return Result<int>::err({Error::Code::InvalidArgument,"invalid translation cache path or map size"});
#ifdef LIMESTONE_HAS_LMDB
  try {
    std::filesystem::create_directories(path);auto storage=std::make_shared<CacheStorage>();
    storage->environment.set_mapsize(map_size);storage->environment.open(path.c_str(),MDB_NOTLS,0600);
    auto txn=lmdb::txn::begin(storage->environment);lmdb::dbi::open(txn,nullptr);txn.commit();
    cache.storage=std::move(storage);cache.entries.clear();return Result<int>::ok(0);
  }catch(const std::exception& e){return Result<int>::err({Error::Code::Internal,std::string("LMDB cache open: ")+e.what()});}
#else
  return Result<int>::err({Error::Code::Unsupported,"persistent caching was disabled at build time"});
#endif
}
Result<Architecture> from_metacode(const metacode::Architecture& source) {
  auto tooling=object(source.fields,"tooling");auto contract=tooling?object(*tooling,"bin2bin"):nullptr;
  if(!contract||text(*contract,"schema_version")!="1")return Result<Architecture>::err({Error::Code::InvalidArgument,"missing or unsupported tooling.bin2bin schema"});
  Architecture out;out.name=source.name;out.version=source.version;out.execution_domain=text(*contract,"execution_domain");
  auto profile=object(source.fields,"profile");
  if(out.execution_domain.empty()||!profile||text(*profile,"execution_model").empty())return Result<Architecture>::err({Error::Code::InvalidArgument,"missing execution domain/state model"});
  out.state_model=text(*profile,"execution_model")+":"+text(*contract,"word_size")+":"+text(*contract,"address_size")+":"+text(*contract,"endianness");
  out.description=metacode::Value(source.fields).text();
  for(auto& reg:source.registers){field(out.description,reg.name);field(out.description,reg.klass);field(out.description,std::to_string(reg.width));field(out.description,std::to_string(reg.number));}
  std::map<std::string,std::string> aliases(source.aliases.begin(),source.aliases.end());
  for(auto&[name,value]:aliases){field(out.description,name);field(out.description,value);}
  std::map<std::string,metacode::Value::Object> encodings(source.encodings.begin(),source.encodings.end());
  for(auto&[name,fields]:encodings){field(out.description,name);field(out.description,metacode::Value(fields).text());}
  if(text(*contract,"instruction_encoding")=="masked")return detail::load_masked(source,std::move(out));
  if(text(*contract,"instruction_encoding")!="fixed8")return Result<Architecture>::err({Error::Code::Unsupported,"no decoder adapter for "+text(*contract,"instruction_encoding")});
  for(auto& op:source.operations) {
    auto encoding=source.encodings.find(text(op.fields,"encoding"));
    if(encoding==source.encodings.end()||text(encoding->second,"width")!="8")return Result<Architecture>::err({Error::Code::Unsupported,"instruction encoding is not a complete byte: "+op.name});
    if(encoding->second.size()!=2||!encoding->second.contains("base"))return Result<Architecture>::err({Error::Code::Unsupported,"operand encodings require a decoder adapter: "+op.name});
    uint64_t code=0;auto& base=encoding->second.at("base");
    if(auto x=std::get_if<uint64_t>(&base.data))code=*x;
    else if(auto x=std::get_if<int64_t>(&base.data)){if(*x<0)return Result<Architecture>::err({Error::Code::InvalidArgument,"negative opcode"});code=*x;}
    else return Result<Architecture>::err({Error::Code::InvalidArgument,"opcode base must be numeric"});
    if(code>255||!out.opcodes.emplace(static_cast<uint8_t>(code),op.name).second)return Result<Architecture>::err({Error::Code::Conflict,"duplicate or out-of-range byte opcode"});
    if(auto operands=op.fields.find("operands");operands!=op.fields.end()&&operands->second.text()!=""&&operands->second.text()!="[]")return Result<Architecture>::err({Error::Code::Unsupported,"operand-bearing instructions require a decoder adapter"});
    auto op_tooling=object(op.fields,"tooling");auto bt=op_tooling?object(*op_tooling,"binary_translation"):nullptr;
    auto opcode=static_cast<uint8_t>(code);
    EncodingForm form;auto checked=detail::translation_contract(bt,form);if(!checked)return Result<Architecture>::err(checked.error());
    if(!form.target_operand.empty())return Result<Architecture>::err({Error::Code::Unsupported,"byte instruction target needs an operand codec: "+op.name});
    if(form.status!=Status::Supported)out.status[opcode]=form.status;
    if(form.control!=ControlFlow::Fallthrough)out.control[opcode]=form.control;
    if(form.status==Status::Supported){auto semantic=normalize(op.semantics);if(!semantic)return Result<Architecture>::err(semantic.error());out.semantics[opcode]=std::move(semantic.value());}
    field(out.description,op.name);field(out.description,metacode::Value(op.fields).text());
  }
  auto valid=validate(out);if(!valid)return Result<Architecture>::err(valid.error());return Result<Architecture>::ok(std::move(out));
}
Result<std::vector<Instruction>> decode(const Architecture& a,std::span<const uint8_t> bytes,uint64_t address) {
  if(a.codec)return detail::decode_native(a,bytes,address);
  if(!a.forms.empty())return detail::decode_masked(a,bytes,address);
  auto valid=validate(a);if(!valid)return Result<std::vector<Instruction>>::err(valid.error());
  if(!bytes.empty()&&bytes.size()-1>std::numeric_limits<uint64_t>::max()-address)return Result<std::vector<Instruction>>::err({Error::Code::InvalidArgument,"instruction address overflow"});
  std::vector<Instruction> out;
  for(size_t i=0;i<bytes.size();++i) {
    auto opcode=bytes[i];Instruction instruction{address+i,opcode,{opcode}};
    auto it=a.opcodes.find(opcode);
    if(it==a.opcodes.end()){instruction.status=Status::Unsupported;instruction.mnemonic=".byte";}
    else {instruction.mnemonic=it->second;instruction.status=status(a,opcode);if(a.control.contains(opcode))instruction.control=a.control.at(opcode);}
    out.push_back(std::move(instruction));
  }
  return Result<std::vector<Instruction>>::ok(std::move(out));
}
Result<std::vector<LiftedInstruction>> lift(const Architecture& a,std::span<const uint8_t> bytes,uint64_t address) {
  auto decoded=decode(a,bytes,address);if(!decoded)return Result<std::vector<LiftedInstruction>>::err(decoded.error());
  std::vector<LiftedInstruction> out;
  for(auto& instruction:decoded.value()) {
    if(instruction.status!=Status::Supported)return Result<std::vector<LiftedInstruction>>::err({Error::Code::Unsupported,"cannot lift instruction at "+std::to_string(instruction.address)});
    auto it=a.semantics.find(instruction.opcode);
    auto semantic=instruction.encoding_id?detail::semantics(a,instruction):normalize(it==a.semantics.end()?"":it->second);if(!semantic)return Result<std::vector<LiftedInstruction>>::err(semantic.error());
    out.push_back({instruction.address,std::move(semantic.value()),instruction.status,instruction.control,instruction.branch_target});
  }
  return Result<std::vector<LiftedInstruction>>::ok(std::move(out));
}
Result<std::vector<uint8_t>> translate(const Architecture& src,const Architecture& dst,std::span<const uint8_t> bytes,TranslationCache* cache,const TranslationOptions& options) {
  if((src.codec&&!src.codec->cacheable)||(dst.codec&&!dst.codec->cacheable))cache=nullptr;
  if(options.semantic_transform){auto& transform=*options.semantic_transform;if(transform.identity.empty()||!transform.apply)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"semantic transformer requires an identity and callable"});if(!transform.cacheable)cache=nullptr;}
   if(options.region_transform){auto& transform=*options.region_transform;if(transform.identity.empty()||!transform.apply)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"region transformer requires an identity and callable"});if(!transform.cacheable)cache=nullptr;}
  // Masked layout remaps the one-past-end source boundary as well as instruction
  // addresses, including when the input itself uses the byte codec.
  if((!src.forms.empty()||!dst.forms.empty())&&bytes.size()>UINT64_MAX-options.source_address)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"source translation address range overflow"});
  auto decoded=decode(src,bytes,options.source_address);if(!decoded)return Result<std::vector<uint8_t>>::err(decoded.error());
  auto valid=validate(dst);if(!valid)return Result<std::vector<uint8_t>>::err(valid.error());
  if(src.forms.empty()&&dst.forms.empty()&&!bytes.empty()&&bytes.size()-1>UINT64_MAX-options.target_address)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"target instruction address overflow"});
  for(auto& instruction:decoded.value())if(instruction.status!=Status::Supported)return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"source instruction is not translatable at "+std::to_string(instruction.address)});
   if(!detail::compatible_states(src,dst,options))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"translation requires matching or explicitly adapted execution/state models"});
   auto key=cache_key(src,dst,bytes,options);
  if(cache){auto hit=cached(*cache,key);if(!hit)return Result<std::vector<uint8_t>>::err(hit.error());if(hit.value())return Result<std::vector<uint8_t>>::ok(std::move(*hit.value()));}
  std::vector<uint8_t> out;
  if(!src.forms.empty()||!dst.forms.empty()){auto translated=detail::translate_masked(src,dst,bytes,options);if(!translated)return translated;out=std::move(translated.value());}
  else {
    if(!detail::compatible_states(src,dst,options))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"translation requires matching or explicitly adapted execution/state models"});
    std::map<std::pair<std::string,ControlFlow>,uint8_t> targets;
    std::map<uint8_t,std::string> names(dst.opcodes.begin(),dst.opcodes.end());
    for(auto&[opcode,name]:names)if(status(dst,opcode)==Status::Supported) {
      auto it=dst.semantics.find(opcode);if(it==dst.semantics.end())continue;
      auto semantic=normalize(it->second);if(!semantic)return Result<std::vector<uint8_t>>::err(semantic.error());
      targets.emplace(std::pair{semantic.value(),dst.control.contains(opcode)?dst.control.at(opcode):ControlFlow::Fallthrough},opcode);
    }
    auto lifted=detail::transformed_region(src,bytes,options);if(!lifted)return Result<std::vector<uint8_t>>::err(lifted.error());
    for(auto& instruction:lifted.value().instructions) {
      auto equivalent=detail::transformed_semantics(options,instruction);if(!equivalent)return Result<std::vector<uint8_t>>::err(equivalent.error());auto canonical=normalize(equivalent.value());if(!canonical)return Result<std::vector<uint8_t>>::err(canonical.error());auto it=targets.find({canonical.value(),instruction.control});
      if(it==targets.end())return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"no semantically equivalent target instruction at "+std::to_string(instruction.address)});
      out.push_back(it->second);
    }
  }
   if(!out.empty()&&out.size()-1>UINT64_MAX-options.target_address)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"transformed target address overflow"});
   if(cache){auto stored=store(*cache,key,out);if(!stored)return Result<std::vector<uint8_t>>::err(stored.error());}
  return Result<std::vector<uint8_t>>::ok(std::move(out));
}
namespace detail {
bool compatible_states(const Architecture& source,const Architecture& target,const TranslationOptions& options) {
  if(source.state_model.empty()||target.state_model.empty()||source.execution_domain.empty()||target.execution_domain.empty())return false;
   if(options.region_transform){auto& t=*options.region_transform;
     if(!t.source_state_model.empty()||!t.target_state_model.empty()||!t.source_domain.empty()||!t.target_domain.empty())
       return t.source_state_model==source.state_model&&t.target_state_model==target.state_model&&t.source_domain==source.execution_domain&&t.target_domain==target.execution_domain;
   }
   return source.state_model==target.state_model&&source.execution_domain==target.execution_domain;
}
Result<SemanticRegion> transformed_region(const Architecture& source,std::span<const uint8_t> bytes,const TranslationOptions& options) {
  if(bytes.size()>UINT64_MAX-options.source_address)return Result<SemanticRegion>::err({Error::Code::InvalidArgument,"semantic region address overflow"});
  auto instructions=lift(source,bytes,options.source_address);if(!instructions)return Result<SemanticRegion>::err(instructions.error());
   SemanticRegion region;region.instructions=std::move(instructions.value());for(size_t k=0;k<region.instructions.size();++k)region.boundaries[region.instructions[k].address]=k;region.boundaries[options.source_address+bytes.size()]=region.instructions.size();
   for(auto& i:region.instructions)if(i.branch_target&&*i.branch_target>=options.source_address&&*i.branch_target<=options.source_address+bytes.size()&&!region.boundaries.contains(*i.branch_target))return Result<SemanticRegion>::err({Error::Code::Conflict,"source branch target is not an instruction boundary"});
  if(!options.region_transform)return Result<SemanticRegion>::ok(std::move(region));
  try {
    auto output=options.region_transform->apply(region);if(!output)return output;
    auto& transformed=output.value();if(transformed.instructions.size()>1048576||transformed.boundaries.size()>1048576)return Result<SemanticRegion>::err({Error::Code::ResourceLimit,"transformed region budget exceeded"});
    for(auto [label,index]:transformed.boundaries)if(index>transformed.instructions.size())return Result<SemanticRegion>::err({Error::Code::Conflict,"transformed label outside instruction stream"});
     auto entry=transformed.boundaries.find(options.source_address),end=transformed.boundaries.find(options.source_address+bytes.size());
     if(entry==transformed.boundaries.end()||end==transformed.boundaries.end()||entry->second!=0||end->second!=transformed.instructions.size())return Result<SemanticRegion>::err({Error::Code::Conflict,"transformed region needs entry/end labels at stream boundaries"});
     for(auto& i:transformed.instructions){
       if(i.status!=Status::Supported)return Result<SemanticRegion>::err({Error::Code::Unsupported,"region transform introduced unsupported semantics"});
       switch(i.control){
         case ControlFlow::Branch:case ControlFlow::ConditionalBranch:if(!i.branch_target)return Result<SemanticRegion>::err({Error::Code::Conflict,"transformed direct branch omits its target"});break;
         case ControlFlow::Call:break;
         case ControlFlow::Fallthrough:case ControlFlow::Return:case ControlFlow::IndirectBranch:case ControlFlow::Trap:if(i.branch_target)return Result<SemanticRegion>::err({Error::Code::Conflict,"transformed non-branch has a direct target"});break;
         default:return Result<SemanticRegion>::err({Error::Code::InvalidArgument,"invalid transformed control flow"});
       }
       auto term=normalize(i.semantics);if(!term)return Result<SemanticRegion>::err(term.error());i.semantics=std::move(term.value());if(i.branch_target&&*i.branch_target>=options.source_address&&*i.branch_target<=options.source_address+bytes.size()&&!transformed.boundaries.contains(*i.branch_target))return Result<SemanticRegion>::err({Error::Code::Conflict,"transformed region omits an internal branch label"});
     }
    return output;
  }catch(const Error& error){return Result<SemanticRegion>::err(error);}catch(const std::exception& error){return Result<SemanticRegion>::err({Error::Code::Internal,error.what()});}catch(...){return Result<SemanticRegion>::err({Error::Code::Internal,"region transform exception"});}
}
}
std::string disassemble(const std::vector<Instruction>& instructions) {
  std::ostringstream out;
  for(auto& i:instructions){out<<std::hex<<i.address<<": "<<i.mnemonic;for(auto& [name,value]:i.operands)out<<" "<<name<<"="<<value;if(i.mnemonic==".byte")out<<" 0x"<<std::setw(2)<<std::setfill('0')<<unsigned(i.opcode);if(i.status!=Status::Supported)out<<" ; unsupported";out<<'\n';}
  return out.str();
}
Result<std::string> decompile(const Architecture& a,std::span<const uint8_t> bytes,uint64_t address) {
  auto lifted=lift(a,bytes,address);if(!lifted)return Result<std::string>::err(lifted.error());
  std::ostringstream out;for(auto& i:lifted.value())out<<std::hex<<i.address<<": "<<i.semantics<<'\n';
  return Result<std::string>::ok(out.str());
}
}
