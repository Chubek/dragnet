#include "codec_internal.hpp"
#include <SExprTk.hpp>
#include <bit>
#include <charconv>
#include <limits>
#include <set>

namespace limestone::bin2bin {
namespace {
using Object=metacode::Value::Object;
[[noreturn]] void fail(std::string text,Error::Code code=Error::Code::InvalidArgument){throw Error{code,std::move(text)};}
uint64_t bits(uint32_t width){return width==64?UINT64_MAX:(uint64_t{1}<<width)-1;}
uint64_t unsigned_number(std::string_view text) {
  uint64_t value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{}||end!=text.data()+text.size())fail("expected unsigned binary operand: "+std::string(text));return value;
}
int64_t signed_number(std::string_view text) {
  int64_t value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{}||end!=text.data()+text.size())fail("expected signed binary operand: "+std::string(text));return value;
}
std::string text(const Object& fields,const std::string& key){auto it=fields.find(key);return it==fields.end()?"":it->second.text();}
const Object* object(const Object& fields,const std::string& key) {
  auto it=fields.find(key);if(it==fields.end())return nullptr;auto out=std::get_if<Object>(&it->second.data);if(!out)fail("binary field must be an object: "+key);return out;
}
uint64_t number(const Object& fields,const std::string& key) {
  auto it=fields.find(key);if(it==fields.end())fail("missing binary field: "+key);
  if(auto n=std::get_if<uint64_t>(&it->second.data))return *n;
  if(auto n=std::get_if<int64_t>(&it->second.data);n&&*n>=0)return static_cast<uint64_t>(*n);
  fail("binary field must be a non-negative integer: "+key);
}
uint32_t small(const Object& fields,const std::string& key) {auto n=number(fields,key);if(n>UINT32_MAX)fail("binary field outside 32-bit range: "+key);return static_cast<uint32_t>(n);}
struct Term {
  std::string atom;bool list=false;std::vector<Term> children;
  bool operator==(const Term&)const=default;
};
Term term(const sexprtk::Cell& cell) {
  if(!cell.tail.empty())fail("quoted/dotted binary semantics need an adapter",Error::Code::Unsupported);
  if(cell.head.is_symbol())return {cell.head.as_string()};
  if(cell.head.is_int())return {std::to_string(cell.head.as_int())};
  if(cell.head.is_list()){Term out;out.list=true;const auto& list=cell.head.as_list();for(size_t k=0;k<list.size();++k)out.children.push_back(term(list[k]));return out;}
  fail("masked semantics support symbolic and integer atoms",Error::Code::Unsupported);
}
Term parse(std::string_view source) {
  if(source.empty())fail("missing instruction semantics",Error::Code::Unsupported);
  size_t depth=0;bool comment=false;
  for(char c:source){if(comment){if(c=='\n')comment=false;continue;}if(c==';'){comment=true;continue;}if(c=='('&&++depth>256)fail("binary semantics nesting limit",Error::Code::ResourceLimit);if(c==')'){if(!depth)fail("unexpected semantic ')'",Error::Code::Parse);--depth;}}
  if(depth)fail("unterminated binary semantics",Error::Code::Parse);
  auto parsed=sexprtk::SExprTk{}.parse(sexprtk::Source::from_string(std::string(source),"<binary-semantics>"));
  if(!parsed.ok()||parsed.root.size()!=1)fail("expected one binary semantic expression",Error::Code::Parse);
  return term(parsed.root.front());
}
std::string render(const Term& t) {
  if(!t.list)return t.atom;std::string out="(";
  for(size_t k=0;k<t.children.size();++k){if(k)out+=' ';out+=render(t.children[k]);}return out+")";
}
Term bind(Term t,const std::map<std::string,std::string>& values) {
  if(!t.list&&t.atom.starts_with('$')){auto it=values.find(t.atom.substr(1));if(it==values.end())fail("unbound semantic operand: "+t.atom);t.atom=it->second;}
  for(auto& child:t.children)child=bind(std::move(child),values);return t;
}
bool match(const Term& pattern,const Term& input,std::map<std::string,std::string>& values) {
  if(!pattern.list&&pattern.atom.starts_with('$')) {
    if(input.list)return false;auto [it,added]=values.emplace(pattern.atom.substr(1),input.atom);return added||it->second==input.atom;
  }
  if(pattern.list!=input.list||pattern.atom!=input.atom||pattern.children.size()!=input.children.size())return false;
  for(size_t k=0;k<pattern.children.size();++k)if(!match(pattern.children[k],input.children[k],values))return false;return true;
}
std::vector<EncodingForm> forms(const Architecture& a) {
  if(!a.forms.empty())return a.forms;
  std::vector<EncodingForm> out;std::map<uint8_t,std::string> sorted(a.opcodes.begin(),a.opcodes.end());
  for(auto& [opcode,name]:sorted){EncodingForm f;f.id=opcode;f.mnemonic=name;f.width=8;f.mask=255;f.base=opcode;if(a.semantics.contains(opcode))f.semantics=a.semantics.at(opcode);if(a.status.contains(opcode))f.status=a.status.at(opcode);if(a.control.contains(opcode))f.control=a.control.at(opcode);out.push_back(std::move(f));}return out;
}
const EncodingForm& form(const Architecture& a,uint32_t id) {
  auto it=std::find_if(a.forms.begin(),a.forms.end(),[&](auto& f){return f.id==id;});if(it==a.forms.end())fail("unknown encoding identity");return *it;
}
uint64_t read_word(std::span<const uint8_t> bytes,bool little) {
  uint64_t value=0;for(size_t k=0;k<bytes.size();++k)value|=uint64_t(bytes[k])<<(8*(little?k:bytes.size()-k-1));return value;
}
int64_t signed_bits(uint64_t value,uint32_t width) {
  if(width<64&&(value&(uint64_t{1}<<(width-1))))value|=~bits(width);return std::bit_cast<int64_t>(value);
}
uint64_t pc_base(uint64_t address,const EncodingForm& f,const EncodingField& field) {
  if(!field.relative_to_end)return address;if(f.width/8>UINT64_MAX-address)fail("PC-relative base overflow");return address+f.width/8;
}
uint64_t add_displacement(uint64_t base,int64_t delta,uint32_t scale) {
  auto magnitude=delta<0?uint64_t(-(delta+1))+1:uint64_t(delta);
  if(magnitude>UINT64_MAX/scale)fail("branch displacement overflow");magnitude*=scale;
  if(delta<0){if(base<magnitude)fail("branch address underflow");return base-magnitude;}
  if(magnitude>UINT64_MAX-base)fail("branch address overflow");return base+magnitude;
}
std::vector<uint8_t> encode_form(const Architecture& a,const EncodingForm& f,const std::map<std::string,std::string>& operands,uint64_t address) {
  if(operands.size()!=f.fields.size())fail("encoding operand count mismatch: "+f.mnemonic);
  uint64_t word=f.base;
  for(auto& field:f.fields) {
    auto it=operands.find(field.name);if(it==operands.end())fail("missing encoding operand: "+field.name);uint64_t value=0;
    if(field.kind==OperandKind::Register) {
      const auto& registers=a.registers.at(field.register_class);auto reg=std::find_if(registers.begin(),registers.end(),[&](auto& r){return r.second==it->second;});
      if(reg==registers.end())fail("unknown register operand: "+it->second);value=reg->first;
    }else if(field.kind==OperandKind::Unsigned)value=unsigned_number(it->second);
    else {
      int64_t n=0;
      if(field.kind==OperandKind::Signed)n=signed_number(it->second);
      else {
        auto target=unsigned_number(it->second),base=pc_base(address,f,field);bool negative=target<base;uint64_t distance=negative?base-target:target-base;
        if(distance%field.scale)fail("unaligned branch displacement");distance/=field.scale;
        if(distance>uint64_t(INT64_MAX)+uint64_t(negative))fail("branch displacement outside signed 64-bit range");
        n=negative?(distance==uint64_t(INT64_MAX)+1?INT64_MIN:-static_cast<int64_t>(distance)):static_cast<int64_t>(distance);
      }
      if(field.width<64){auto bound=int64_t{1}<<(field.width-1);if(n< -bound||n>=bound)fail("signed operand out of range: "+field.name);}
      value=std::bit_cast<uint64_t>(n)&bits(field.width);
    }
    if(value>bits(field.width))fail("operand out of range: "+field.name);word|=value<<field.lsb;
  }
  std::vector<uint8_t> out(f.width/8);bool little=a.endianness!="big";
  for(size_t k=0;k<out.size();++k)out[k]=static_cast<uint8_t>(word>>(8*(little?k:out.size()-k-1)));return out;
}
}

Result<int> validate(const Architecture& a) {
  try {
    if(a.name.empty())fail("architecture has no identity");
    if(a.codec&&(a.codec->identity.empty()||!a.codec->decode_one||!a.codec->encode_form||!a.codec->max_instruction_bytes||a.codec->max_instruction_bytes>1048576||a.forms.empty()))fail("native codec requires complete owning callbacks, forms and bounds");
    if(a.forms.empty()) {
      for(auto& [opcode,name]:a.opcodes)if(name.empty())fail("empty byte instruction mnemonic");
      for(auto [opcode,status]:a.status)if(!a.opcodes.contains(opcode)||status<Status::Supported||status>Status::Ambiguous)fail("invalid byte instruction translation status");
      for(auto [opcode,flow]:a.control)if(!a.opcodes.contains(opcode)||flow<ControlFlow::Fallthrough||flow>ControlFlow::Trap)fail("invalid byte instruction control flow");else if(flow==ControlFlow::Branch||flow==ControlFlow::ConditionalBranch||flow==ControlFlow::Call)fail("direct byte control flow requires an operand codec",Error::Code::Unsupported);
      return Result<int>::ok(0);
    }
    if(a.endianness!="little"&&a.endianness!="big")fail("masked encodings require explicit endianness");
    std::set<uint32_t> ids;
    for(auto& f:a.forms) {
      if(f.status<Status::Supported||f.status>Status::Ambiguous||f.control<ControlFlow::Fallthrough||f.control>ControlFlow::Trap)fail("invalid encoding semantic classification");
      if(!ids.insert(f.id).second||f.mnemonic.empty()||!f.width||(!a.codec&&f.width>64)||(a.codec&&f.width/8>a.codec->max_instruction_bytes)||f.width%8)fail("invalid encoding form identity or width");
      if(a.codec&&(f.mask||f.base))fail("native forms use adapter-owned encoding, not masked bits");
      if(!a.codec&&((f.base&~f.mask)||(f.mask&~bits(f.width))))fail("encoding base/mask outside fixed bits");
      uint64_t covered=f.mask;std::set<std::string> names;
      for(auto& field:f.fields) {
        if(field.kind<OperandKind::Unsigned||field.kind>OperandKind::PCRelative)fail("unknown encoding field kind");
        if(field.name.empty()||!names.insert(field.name).second||!field.width||field.width>64||(!a.codec&&(field.lsb>=f.width||field.width>f.width-field.lsb))||(a.codec&&field.lsb)||!field.scale)fail("invalid encoding field: "+field.name);
        if(!a.codec){auto mask=bits(field.width)<<field.lsb;if(covered&mask)fail("overlapping encoding field: "+field.name);covered|=mask;}
        if(field.kind==OperandKind::Register) {
          auto regs=a.registers.find(field.register_class);if(regs==a.registers.end()||regs->second.empty())fail("missing register encoding class: "+field.register_class);
          std::set<std::string> register_names;for(auto& [n,name]:regs->second)if(n>bits(field.width)||name.empty()||!register_names.insert(name).second)fail("invalid register encoding table");
        }else if(!field.register_class.empty())fail("non-register field has register class");
        if(field.kind!=OperandKind::PCRelative&&(field.scale!=1||!field.relative_to_end))fail("PC policy on non-branch field");
      }
      if(!a.codec&&covered!=bits(f.width))fail("encoding contains undescribed bits",Error::Code::Unsupported);
      bool direct=f.control==ControlFlow::Branch||f.control==ControlFlow::ConditionalBranch||f.control==ControlFlow::Call;
      if(direct!=!f.target_operand.empty()||(direct&&!names.contains(f.target_operand)))fail("invalid branch target contract");
      if(direct){auto field=std::find_if(f.fields.begin(),f.fields.end(),[&](auto& x){return x.name==f.target_operand;});if(field->kind!=OperandKind::PCRelative&&field->kind!=OperandKind::Unsigned)fail("branch target must be absolute or PC-relative");}
      if(f.status==Status::Supported) {
        auto t=parse(f.semantics);std::set<std::string> used;
        std::function<void(const Term&)> check=[&](auto& n){if(!n.list&&n.atom.starts_with('$')){auto key=n.atom.substr(1);if(!names.contains(key))fail("unknown semantic operand: "+n.atom);used.insert(key);}for(auto& c:n.children)check(c);};check(t);
        if(used!=names)fail("translatable encoding operands need complete semantic bindings",Error::Code::Unsupported);
      }
    }
    // Prefix-free masks make instruction boundaries unambiguous, including
    // mixed-width encodings. No longest-match guess is used.
    if(!a.codec)for(size_t x=0;x<a.forms.size();++x)for(size_t y=x+1;y<a.forms.size();++y) {
      auto& p=a.forms[x];auto& q=a.forms[y];bool compatible=true;
      for(uint32_t k=0;k<std::min(p.width,q.width)/8;++k) {
        auto ps=8*(a.endianness=="little"?k:p.width/8-k-1),qs=8*(a.endianness=="little"?k:q.width/8-k-1);
        auto pm=(p.mask>>ps)&255,qm=(q.mask>>qs)&255;
        if(((p.base>>ps)^(q.base>>qs))&pm&qm)compatible=false;
      }
      if(compatible)fail("ambiguous or non-prefix-free instruction encodings",Error::Code::Conflict);
    }
    return Result<int>::ok(0);
  }catch(const Error& e){return Result<int>::err(e);}
}
Result<std::vector<uint8_t>> encode(const Architecture& a,std::string_view mnemonic,const std::map<std::string,std::string>& operands,uint64_t address) {
  auto checked=validate(a);if(!checked)return Result<std::vector<uint8_t>>::err(checked.error());
  auto alternatives=forms(a);std::sort(alternatives.begin(),alternatives.end(),[](auto& x,auto& y){return std::tie(x.width,x.id)<std::tie(y.width,y.id);});
  std::optional<Error> rejected;
  for(auto& f:alternatives)if(f.mnemonic==mnemonic){if(a.codec){auto encoded=detail::encode_native(a,f.id,operands,address);if(encoded)return encoded;rejected=encoded.error();}else try{return Result<std::vector<uint8_t>>::ok(encode_form(a,f,operands,address));}catch(const Error& e){rejected=e;}}
  return Result<std::vector<uint8_t>>::err(rejected.value_or(Error{Error::Code::NotFound,"unknown encoding mnemonic: "+std::string(mnemonic)}));
}

Result<std::vector<uint8_t>> encode_form(const Architecture& a,uint32_t id,const std::map<std::string,std::string>& operands,uint64_t address) {
  auto valid=validate(a);if(!valid)return Result<std::vector<uint8_t>>::err(valid.error());
  if(a.codec)return detail::encode_native(a,id,operands,address);
  try{auto alternatives=forms(a);auto found=std::find_if(alternatives.begin(),alternatives.end(),[&](auto& f){return f.id==id;});if(found==alternatives.end())fail("unknown encoding form",Error::Code::NotFound);return Result<std::vector<uint8_t>>::ok(encode_form(a,*found,operands,address));}catch(const Error& e){return Result<std::vector<uint8_t>>::err(e);}
}

namespace detail {
Result<std::string> transformed_semantics(const TranslationOptions& options,const LiftedInstruction& instruction) {
  if(!options.semantic_transform)return Result<std::string>::ok(instruction.semantics);
  try{return options.semantic_transform->apply(instruction);}
  catch(const Error& e){return Result<std::string>::err(e);}
  catch(const std::exception& e){return Result<std::string>::err({Error::Code::Internal,std::string("binary semantic adapter: ")+e.what()});}
  catch(...){return Result<std::string>::err({Error::Code::Internal,"binary semantic adapter exception"});}
}
Result<int> translation_contract(const Object* contract,EncodingForm& form) {
  try {
    form.status=contract&&text(*contract,"schema_version")=="1"&&text(*contract,"equivalence_basis")=="semantics"?Status::Supported:Status::Unsupported;
    if(!contract)return Result<int>::ok(0);
    auto status=text(*contract,"status");const std::map<std::string,Status> states{{"supported",Status::Supported},{"unsupported",Status::Unsupported},{"fallback",Status::Fallback},{"architecture_specific",Status::ArchitectureSpecific},{"privileged",Status::Privileged},{"environment_dependent",Status::EnvironmentDependent},{"ambiguous",Status::Ambiguous}};
    if(!status.empty()){if(!states.contains(status))fail("unknown instruction translation status");if(status=="supported"&&form.status!=Status::Supported)fail("supported instruction lacks semantic equivalence contract");form.status=states.at(status);}
    auto flow=text(*contract,"control_flow");const std::map<std::string,ControlFlow> flows{{"",ControlFlow::Fallthrough},{"fallthrough",ControlFlow::Fallthrough},{"branch",ControlFlow::Branch},{"conditional_branch",ControlFlow::ConditionalBranch},{"call",ControlFlow::Call},{"return",ControlFlow::Return},{"indirect_branch",ControlFlow::IndirectBranch},{"trap",ControlFlow::Trap}};
    if(!flows.contains(flow))fail("unsupported control-flow contract",Error::Code::Unsupported);form.control=flows.at(flow);form.target_operand=text(*contract,"target_operand");
    return Result<int>::ok(0);
  }catch(const Error& e){return Result<int>::err(e);}
}
Result<Architecture> load_masked(const metacode::Architecture& source,Architecture out) {
  try {
    const auto& contract=*object(*object(source.fields,"tooling"),"bin2bin");out.endianness=text(contract,"endianness");
    for(auto& r:source.registers)if(!out.registers[r.klass].emplace(r.number,r.name).second)fail("duplicate encoded register number",Error::Code::Conflict);
    auto operations=source.operations;std::sort(operations.begin(),operations.end(),[](auto& a,auto& b){return a.name<b.name;});uint32_t id=0;
    for(auto& op:operations) {
      auto found=source.encodings.find(text(op.fields,"encoding"));if(found==source.encodings.end())fail("missing instruction encoding: "+op.name);
      auto& encoding=found->second;EncodingForm f;f.id=id++;f.mnemonic=op.name;f.width=small(encoding,"width");f.base=number(encoding,"base");f.mask=number(encoding,"mask");f.semantics=op.semantics;f.origin=op.source.file+":"+std::to_string(op.source.line);
      for(auto& [key,v]:encoding)if(key!="width"&&key!="base"&&key!="mask"&&key!="fields")fail("unsupported masked encoding property: "+key,Error::Code::Unsupported);
      if(auto fields=object(encoding,"fields")) {
        std::map<std::string,metacode::Value> sorted(fields->begin(),fields->end());
        for(auto& [name,value]:sorted) {
          auto field=std::get_if<Object>(&value.data);if(!field)fail("encoding field must be an object");
          EncodingField member;member.name=name;member.lsb=small(*field,"lsb");member.width=small(*field,"width");
          auto kind=text(*field,"kind");if(kind=="signed")member.kind=OperandKind::Signed;else if(kind=="register")member.kind=OperandKind::Register;else if(kind=="pc_relative")member.kind=OperandKind::PCRelative;else if(kind!="unsigned")fail("unknown encoding operand kind: "+kind);
          member.register_class=text(*field,"class");if(field->contains("scale"))member.scale=small(*field,"scale");
          if(field->contains("pc_base")){auto base=text(*field,"pc_base");if(base!="start"&&base!="end")fail("unknown PC base");member.relative_to_end=base=="end";}
          for(auto& [key,v]:*field)if(key!="lsb"&&key!="width"&&key!="kind"&&key!="class"&&key!="scale"&&key!="pc_base")fail("unsupported encoding field property: "+key,Error::Code::Unsupported);
          f.fields.push_back(std::move(member));
        }
      }
      const auto* tooling=object(op.fields,"tooling");const auto* bt=tooling?object(*tooling,"binary_translation"):nullptr;
      auto contract_valid=translation_contract(bt,f);if(!contract_valid)return Result<Architecture>::err(contract_valid.error());
      out.forms.push_back(std::move(f));
      out.description+=op.name+":"+metacode::Value(op.fields).text();
    }
    auto valid=validate(out);if(!valid)return Result<Architecture>::err(valid.error());if(out.forms.empty())fail("masked architecture contains no encoding forms");
    return Result<Architecture>::ok(std::move(out));
  }catch(const Error& e){return Result<Architecture>::err(e);}
}
Result<std::vector<Instruction>> decode_masked(const Architecture& a,std::span<const uint8_t> bytes,uint64_t address) {
  auto valid=validate(a);if(!valid)return Result<std::vector<Instruction>>::err(valid.error());
  try {
    if(bytes.size()>UINT64_MAX-address)fail("binary address range overflow");
    std::vector<Instruction> out;size_t offset=0;
    while(offset<bytes.size()) {
      const EncodingForm* selected=nullptr;uint64_t word=0;bool truncated=false;
      for(auto& f:a.forms) {
        auto length=f.width/8;
        if(length>bytes.size()-offset) {
          bool prefix=true;for(size_t k=0;k<bytes.size()-offset;++k){auto shift=8*(a.endianness=="little"?k:length-k-1);if((uint64_t(bytes[offset+k])^(f.base>>shift))&((f.mask>>shift)&255))prefix=false;}truncated|=prefix;continue;
        }
        auto value=read_word(bytes.subspan(offset,length),a.endianness=="little");if((value&f.mask)==f.base){if(selected)fail("ambiguous instruction encoding",Error::Code::Conflict);selected=&f;word=value;}
      }
      if(!selected)fail((truncated?"truncated":"unknown")+std::string(" instruction at ")+std::to_string(address+offset),truncated?Error::Code::Parse:Error::Code::Unsupported);
      const auto& f=*selected;Instruction i{address+offset,bytes[offset],{},f.mnemonic,f.status};i.encoding_id=f.id;i.control=f.control;i.bytes.assign(bytes.begin()+offset,bytes.begin()+offset+f.width/8);
      for(auto& field:f.fields) {
        auto value=(word>>field.lsb)&bits(field.width);std::string operand;
        if(field.kind==OperandKind::Register){const auto& regs=a.registers.at(field.register_class);auto r=regs.find(value);if(r==regs.end())fail("unknown encoded register at "+std::to_string(i.address));operand=r->second;}
        else if(field.kind==OperandKind::Signed)operand=std::to_string(signed_bits(value,field.width));
        else if(field.kind==OperandKind::PCRelative)operand=std::to_string(add_displacement(pc_base(i.address,f,field),signed_bits(value,field.width),field.scale));
        else operand=std::to_string(value);
        i.operands[field.name]=std::move(operand);
      }
      if(!f.target_operand.empty())i.branch_target=unsigned_number(i.operands.at(f.target_operand));out.push_back(std::move(i));offset+=f.width/8;
    }
    return Result<std::vector<Instruction>>::ok(std::move(out));
  }catch(const Error& e){return Result<std::vector<Instruction>>::err(e);}
}
Result<std::string> semantics(const Architecture& a,const Instruction& instruction) {
  try {
    if(instruction.status!=Status::Supported)fail("instruction is not semantically supported",Error::Code::Unsupported);
    if(!instruction.encoding_id)fail("missing masked encoding identity");
    return Result<std::string>::ok(render(bind(parse(form(a,*instruction.encoding_id).semantics),instruction.operands)));
  }catch(const Error& e){return Result<std::string>::err(e);}
}
Result<std::vector<uint8_t>> translate_masked(const Architecture& src,const Architecture& dst,std::span<const uint8_t> bytes,const TranslationOptions& options) {
  try {
    if(!compatible_states(src,dst,options))fail("translation requires matching or explicitly adapted execution/state models",Error::Code::Unsupported);
    auto valid=validate(dst);if(!valid)return Result<std::vector<uint8_t>>::err(valid.error());
    auto region=transformed_region(src,bytes,options);if(!region)return Result<std::vector<uint8_t>>::err(region.error());
    struct Alternative {EncodingForm encoding;std::map<std::string,std::string> operands;};
    struct Plan {std::vector<Alternative> alternatives;size_t chosen=0;uint64_t address=0;};
    auto targets=forms(dst);std::sort(targets.begin(),targets.end(),[](auto& a,auto& b){return std::tie(a.width,a.id)<std::tie(b.width,b.id);});
    std::vector<Plan> plans;std::map<uint64_t,uint64_t> addresses;
    for(auto& i:region.value().instructions) {
      if(i.status!=Status::Supported)fail("unsupported source instruction at "+std::to_string(i.address),Error::Code::Unsupported);
      auto equivalent=transformed_semantics(options,i);if(!equivalent)return Result<std::vector<uint8_t>>::err(equivalent.error());auto input=parse(equivalent.value());
      Plan plan;
      for(auto& f:targets)if(f.status==Status::Supported&&f.control==i.control&&!f.semantics.empty()) {
        std::map<std::string,std::string> operands;if(!match(parse(f.semantics),input,operands))continue;
        if(i.branch_target&&(!operands.contains(f.target_operand)||unsigned_number(operands.at(f.target_operand))!=*i.branch_target))continue;
        // Probe non-target operands without prematurely committing to a branch
        // displacement. Final form selection follows layout and address remapping.
        auto probe=operands;if(!f.target_operand.empty()){auto field=std::find_if(f.fields.begin(),f.fields.end(),[&](auto& n){return n.name==f.target_operand;});probe[f.target_operand]=std::to_string(field->kind==OperandKind::PCRelative&&field->relative_to_end?f.width/8:0);}
         if(!bin2bin::encode_form(dst,f.id,probe,0))continue;
        plan.alternatives.push_back({f,std::move(operands)});
      }
      if(plan.alternatives.empty())fail("no semantically equivalent target encoding at "+std::to_string(i.address),Error::Code::Unsupported);plans.push_back(std::move(plan));
    }
    bool promoted=true;
    while(promoted) {
      promoted=false;uint64_t address=options.target_address;addresses.clear();
      for(auto& p:plans){auto& f=p.alternatives[p.chosen].encoding;p.address=address;if(f.width/8>UINT64_MAX-address)fail("translated address overflow");address+=f.width/8;}
      for(auto [label,index]:region.value().boundaries)addresses[label]=index==plans.size()?address:plans[index].address;
      std::vector<uint8_t> out;
      for(size_t k=0;k<plans.size();++k) {
        auto& p=plans[k];const auto& i=region.value().instructions[k];bool found=false;std::optional<Error> rejected;
        for(size_t choice=p.chosen;choice<p.alternatives.size();++choice) {
          auto& alternative=p.alternatives[choice];auto operands=alternative.operands;
          if(i.branch_target) {
            auto target=*i.branch_target;
            if(addresses.contains(target)|| (target>=options.source_address&&target<=options.source_address+bytes.size())) {
              if(!addresses.contains(target))fail("branch target is not an instruction boundary",Error::Code::Conflict);operands[alternative.encoding.target_operand]=std::to_string(addresses.at(target));
            }
          }
           auto encoded=bin2bin::encode_form(dst,alternative.encoding.id,operands,p.address);if(!encoded){rejected=encoded.error();continue;}if(choice!=p.chosen){p.chosen=choice;promoted=true;}out.insert(out.end(),encoded.value().begin(),encoded.value().end());found=true;break;
        }
        if(!found){if(promoted)break;throw rejected.value_or(Error{Error::Code::Unsupported,"no encodable branch form"});}
      }
      // Promotion is monotonic, so finite alternative lists bound relaxation.
      if(!promoted)return Result<std::vector<uint8_t>>::ok(std::move(out));
    }
    fail("unreachable translation layout state",Error::Code::Internal);
  }catch(const Error& e){return Result<std::vector<uint8_t>>::err(e);}
}
}
Result<ControlFlowGraph> analyze(const Architecture& a,std::span<const uint8_t> bytes,uint64_t address) {
  if(bytes.size()>UINT64_MAX-address)return Result<ControlFlowGraph>::err({Error::Code::InvalidArgument,"CFG address range overflow"});
  auto decoded=decode(a,bytes,address);if(!decoded)return Result<ControlFlowGraph>::err(decoded.error());
  try {
    ControlFlowGraph result;result.instructions=std::move(decoded.value());if(result.instructions.empty())return Result<ControlFlowGraph>::ok(std::move(result));
    std::map<uint64_t,size_t> index;for(size_t k=0;k<result.instructions.size();++k)index[result.instructions[k].address]=k;
    std::set<size_t> leaders{0};uint64_t end=address+bytes.size();
    for(size_t k=0;k<result.instructions.size();++k) {
      auto& i=result.instructions[k];if(i.status!=Status::Supported)fail("CFG needs a supported control-flow contract at "+std::to_string(i.address),Error::Code::Unsupported);if(i.branch_target&&*i.branch_target>=address&&*i.branch_target<end){if(!index.contains(*i.branch_target))fail("branch target is not an instruction boundary",Error::Code::Conflict);leaders.insert(index.at(*i.branch_target));}
      if(i.control!=ControlFlow::Fallthrough&&k+1<result.instructions.size())leaders.insert(k+1);
    }
    for(auto first:leaders) {
      BasicBlock block;block.address=result.instructions[first].address;auto next=leaders.upper_bound(first);size_t stop=next==leaders.end()?result.instructions.size():*next;
      for(size_t k=first;k<stop;++k)block.instructions.push_back(k);auto& last=result.instructions[stop-1];
      if(last.branch_target)block.successors.push_back(*last.branch_target);
      if(last.control==ControlFlow::Fallthrough||last.control==ControlFlow::ConditionalBranch||last.control==ControlFlow::Call)if(stop<result.instructions.size())block.successors.push_back(result.instructions[stop].address);
      block.indirect_exit=last.control==ControlFlow::IndirectBranch;std::sort(block.successors.begin(),block.successors.end());block.successors.erase(std::unique(block.successors.begin(),block.successors.end()),block.successors.end());result.blocks.push_back(std::move(block));
    }
    return Result<ControlFlowGraph>::ok(std::move(result));
  }catch(const Error& e){return Result<ControlFlowGraph>::err(e);}
}
}
