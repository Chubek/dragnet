#include "codec_internal.hpp"
#include <charconv>

namespace limestone::bin2bin {
namespace {
using V=metacode::Value;
using Object=V::Object;
[[noreturn]] void fail(std::string message,Error::Code code=Error::Code::InvalidArgument){throw Error{code,std::move(message)};}
const Object& object(const Object& input,const std::string& name){auto found=input.find(name);auto value=found==input.end()?nullptr:std::get_if<Object>(&found->second.data);if(!value)fail("native codec needs object: "+name);return *value;}
std::string text(const Object& input,const std::string& name){auto found=input.find(name);auto value=found==input.end()?nullptr:std::get_if<std::string>(&found->second.data);if(!value||value->empty()||value->find('\0')!=value->npos)fail("native codec needs string: "+name);return *value;}
uint32_t number(const Object& input,const std::string& name){auto found=input.find(name);if(found==input.end())fail("missing native codec quantity: "+name);uint64_t value;if(auto p=std::get_if<uint64_t>(&found->second.data))value=*p;else if(auto p=std::get_if<int64_t>(&found->second.data);p&&*p>=0)value=uint64_t(*p);else fail("invalid native codec quantity: "+name);if(value>UINT32_MAX)fail("native codec quantity overflow: "+name);return uint32_t(value);}
template<class T> T integer(const std::string& input){T result{};auto [end,error]=std::from_chars(input.data(),input.data()+input.size(),result);if(error!=std::errc{}||end!=input.data()+input.size())fail("invalid native codec operand: "+input);return result;}
const EncodingForm& form(const Architecture& architecture,uint32_t id){auto found=std::find_if(architecture.forms.begin(),architecture.forms.end(),[&](auto& f){return f.id==id;});if(found==architecture.forms.end())fail("native codec returned an unknown form",Error::Code::Conflict);return *found;}
void operands(const Architecture& architecture,const EncodingForm& f,const std::map<std::string,std::string>& values){
  if(values.size()!=f.fields.size())fail("native operand count mismatch: "+f.mnemonic);
  for(auto& field:f.fields){auto found=values.find(field.name);if(found==values.end())fail("missing native operand: "+field.name);auto& value=found->second;
    if(field.kind==OperandKind::Register){auto& names=architecture.registers.at(field.register_class);if(std::none_of(names.begin(),names.end(),[&](auto& r){return r.second==value;}))fail("unknown native register: "+value);}
    else if(field.kind==OperandKind::Signed){auto n=integer<int64_t>(value);if(field.width<64){auto bound=int64_t{1}<<(field.width-1);if(n< -bound||n>=bound)fail("native signed operand out of range");}}
    else {auto n=integer<uint64_t>(value);if(field.kind==OperandKind::Unsigned&&field.width<64&&n>=(uint64_t{1}<<field.width))fail("native unsigned operand out of range");}
  }
}
template<class T,class F> Result<T> boundary(F&& callback){try{return callback();}catch(const Error& error){return Result<T>::err(error);}catch(const std::bad_alloc&){return Result<T>::err({Error::Code::ResourceLimit,"native codec allocation failed"});}catch(const std::exception& error){return Result<T>::err({Error::Code::Internal,error.what()});}catch(...){return Result<T>::err({Error::Code::Internal,"native codec exception"});}}
}
Result<Architecture> from_metacode(const metacode::Architecture& source,const CodecAdapter& adapter){
  return boundary<Architecture>([&]()->Result<Architecture>{
    const auto& contract=object(object(source.fields,"tooling"),"bin2bin");
    if(number(contract,"schema_version")!=1||text(contract,"instruction_encoding")!="native")fail("native codec needs schema version 1 and native encoding");
    if(text(contract,"codec_adapter")!=adapter.identity)fail("native codec identity differs from authoritative metadata",Error::Code::Conflict);
    Architecture result;result.name=source.name;result.version=source.version;result.description=V(source.fields).text();result.execution_domain=text(contract,"execution_domain");result.endianness=text(contract,"endianness");
    result.state_model=text(object(source.fields,"profile"),"execution_model")+":"+std::to_string(number(contract,"word_size"))+":"+std::to_string(number(contract,"address_size"))+":"+result.endianness;
    result.codec=std::make_shared<const CodecAdapter>(adapter);
    for(auto& reg:source.registers)if(!result.registers[reg.klass].emplace(reg.number,reg.name).second)fail("duplicate native register number",Error::Code::Conflict);
    auto operations=source.operations;std::sort(operations.begin(),operations.end(),[](auto& a,auto& b){return a.name<b.name;});uint64_t next=0;
    for(auto& operation:operations){auto name=text(operation.fields,"encoding");auto encoding=source.encodings.find(name);if(encoding==source.encodings.end())fail("missing native encoding: "+name);auto& fields=encoding->second;
      if(next>UINT32_MAX)fail("native form identity overflow",Error::Code::ResourceLimit);EncodingForm f;f.id=uint32_t(next++);f.mnemonic=operation.name;f.width=number(fields,"width");f.semantics=operation.semantics;f.origin=operation.source.file+":"+std::to_string(operation.source.line);
      for(auto& [key,value]:fields)if(key!="width"&&key!="operands")fail("unsupported native encoding property: "+key,Error::Code::Unsupported);
      if(fields.contains("operands"))for(auto& [key,value]:object(fields,"operands")){auto description=std::get_if<Object>(&value.data);if(!description)fail("native operand description must be an object");EncodingField operand;operand.name=key;operand.width=number(*description,"width");auto kind=text(*description,"kind");
        if(kind=="signed")operand.kind=OperandKind::Signed;else if(kind=="unsigned")operand.kind=OperandKind::Unsigned;else if(kind=="register"){operand.kind=OperandKind::Register;operand.register_class=text(*description,"class");}else if(kind=="pc_relative")operand.kind=OperandKind::PCRelative;else fail("unknown native operand kind");
        if(description->contains("scale"))operand.scale=number(*description,"scale");if(description->contains("pc_base")){auto policy=text(*description,"pc_base");if(policy!="start"&&policy!="end")fail("invalid native PC base");operand.relative_to_end=policy=="end";}
        for(auto& [property,v]:*description)if(property!="width"&&property!="kind"&&property!="class"&&property!="scale"&&property!="pc_base")fail("unsupported native operand property: "+property,Error::Code::Unsupported);
        f.fields.push_back(std::move(operand));
      }
      auto checked=detail::translation_contract(&object(object(operation.fields,"tooling"),"binary_translation"),f);if(!checked)return Result<Architecture>::err(checked.error());result.forms.push_back(std::move(f));
      result.description+=operation.name+V(operation.fields).text()+V(fields).text();
    }
    auto checked=validate(result);if(!checked)return Result<Architecture>::err(checked.error());return Result<Architecture>::ok(std::move(result));
  });
}
namespace detail {
Result<std::vector<uint8_t>> encode_native(const Architecture& architecture,uint32_t id,const std::map<std::string,std::string>& values,uint64_t address){
  return boundary<std::vector<uint8_t>>([&]()->Result<std::vector<uint8_t>>{
    auto codec=architecture.codec;auto& f=form(architecture,id);operands(architecture,f,values);if(f.width/8>UINT64_MAX-address)fail("native encoding address overflow");
    auto encoded=codec->encode_form(id,values,address);if(!encoded)return encoded;
    if(encoded.value().size()!=f.width/8)fail("native encoder changed its declared form length",Error::Code::Conflict);
    auto decoded=codec->decode_one(encoded.value(),address);if(!decoded)return Result<std::vector<uint8_t>>::err(decoded.error());
    if(decoded.value().form!=id||decoded.value().operands!=values)fail("native encoder failed operand/form round trip",Error::Code::Conflict);
    return encoded;
  });
}
Result<std::vector<Instruction>> decode_native(const Architecture& architecture,std::span<const uint8_t> bytes,uint64_t address){
  return boundary<std::vector<Instruction>>([&]()->Result<std::vector<Instruction>>{
    auto checked=validate(architecture);if(!checked)return Result<std::vector<Instruction>>::err(checked.error());if(bytes.size()>UINT64_MAX-address)fail("native decoding address overflow");
    auto codec=architecture.codec;std::vector<Instruction> result;size_t offset=0;
    while(offset<bytes.size()){auto decoded=codec->decode_one(bytes.subspan(offset,std::min(bytes.size()-offset,codec->max_instruction_bytes)),address+offset);if(!decoded)return Result<std::vector<Instruction>>::err(decoded.error());auto& f=form(architecture,decoded.value().form);auto length=f.width/8;if(length>bytes.size()-offset)fail("truncated native instruction",Error::Code::Parse);operands(architecture,f,decoded.value().operands);
      auto encoded=codec->encode_form(f.id,decoded.value().operands,address+offset);if(!encoded)return Result<std::vector<Instruction>>::err(encoded.error());if(encoded.value().size()!=length||!std::equal(encoded.value().begin(),encoded.value().end(),bytes.begin()+offset))fail("native decoder failed byte round trip",Error::Code::Conflict);
      Instruction instruction{address+offset,bytes[offset],std::move(encoded.value()),f.mnemonic,f.status};instruction.encoding_id=f.id;instruction.operands=std::move(decoded.value().operands);instruction.control=f.control;if(!f.target_operand.empty())instruction.branch_target=integer<uint64_t>(instruction.operands.at(f.target_operand));result.push_back(std::move(instruction));offset+=length;
    }
    return Result<std::vector<Instruction>>::ok(std::move(result));
  });
}
}
}
