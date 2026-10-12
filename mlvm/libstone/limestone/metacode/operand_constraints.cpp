#include "operand_constraints.hpp"
#include <map>
#include <set>

namespace limestone::metacode {
namespace {
[[noreturn]] void fail(const Value& input,std::string message,Error::Code code=Error::Code::InvalidArgument) {
  if(!input.source.file.empty())message=input.source.file+":"+std::to_string(input.source.line)+":"+std::to_string(input.source.column)+": "+message;
  throw Error{code,std::move(message)};
}
const Value& field(const Value::Object& object,const std::string& name) {
  auto found=object.find(name);if(found==object.end())throw Error{Error::Code::InvalidArgument,"missing operand constraint field: "+name};return found->second;
}
std::string symbol(const Value& value) {
  auto text=std::get_if<std::string>(&value.data);if(!text||text->empty())fail(value,"operand constraint name must be a nonempty string");return *text;
}
}
Result<std::vector<OperandConstraint>> load_operand_constraints(const Value::Object& fields) {
  try {
    for(auto& [key,value]:fields)if(key!="constraints"&&key!="predicates")fail(value,"unknown where contract field: "+key,Error::Code::Unsupported);
    if(!fields.contains("constraints")){if(!fields.contains("predicates"))throw Error{Error::Code::InvalidArgument,"empty where contract"};return Result<std::vector<OperandConstraint>>::ok({});}
    const auto& input=fields.at("constraints");auto array=std::get_if<Value::Array>(&input.data);if(!array)fail(input,"operand constraints must be an array");
    std::vector<OperandConstraint> constraints;
    for(auto& entry:*array) {
      auto object=std::get_if<Value::Object>(&entry.data);if(!object)fail(entry,"operand constraint must be an object");
      auto kind=symbol(field(*object,"kind"));auto name=std::find(std::begin(operand_predicate_names),std::end(operand_predicate_names),kind);
      if(name==std::end(operand_predicate_names))fail(entry,"unknown operand predicate: "+kind,Error::Code::Unsupported);
      OperandConstraint constraint{static_cast<OperandPredicate>(name-std::begin(operand_predicate_names)),symbol(field(*object,"operand"))};
      if(auto other=object->find("other");other!=object->end())constraint.other=symbol(other->second);
      if(auto value=object->find("value");value!=object->end()) {
        if(auto n=std::get_if<int64_t>(&value->second.data))constraint.value=*n;
        else if(auto n=std::get_if<uint64_t>(&value->second.data);n&&*n<=uint64_t(INT64_MAX))constraint.value=int64_t(*n);
        else fail(value->second,"operand constraint value must be a signed 64-bit integer");
      }
      std::map<std::string,Value> ordered(object->begin(),object->end());
      for(auto& [key,value]:ordered)if(key!="kind"&&key!="operand"&&key!="other"&&key!="value")fail(value,"unknown operand constraint field: "+key,Error::Code::Unsupported);
      std::vector<std::string> bindings{constraint.operand,constraint.other};auto valid=validate_operand_constraints(std::span{&constraint,1},bindings);if(!valid)fail(entry,valid.error().message,valid.error().code);
      constraints.push_back(std::move(constraint));
    }
    return Result<std::vector<OperandConstraint>>::ok(std::move(constraints));
  }catch(const Error& error){return Result<std::vector<OperandConstraint>>::err(error);}
}
Value::Object operand_constraints_metadata(std::span<const OperandConstraint> constraints) {
  Value::Array array;
  for(auto& constraint:constraints) {
    Value::Object entry{{"kind",Value(std::string(predicate_name(constraint.predicate)))},{"operand",Value(constraint.operand)}};
    if(!constraint.other.empty())entry["other"]=Value(constraint.other);
    if(constraint.value)entry["value"]=Value(*constraint.value);
    array.emplace_back(std::move(entry));
  }
  return {{"constraints",Value(std::move(array))}};
}
Result<std::vector<HostConstraint>> load_host_constraints(const Value::Object& fields) {
  using Output=Result<std::vector<HostConstraint>>;
  try {
    for(auto& [key,value]:fields)if(key!="constraints"&&key!="predicates")fail(value,"unknown where contract field: "+key,Error::Code::Unsupported);
    if(!fields.contains("predicates"))return Output::ok({});const auto& input=fields.at("predicates");auto array=std::get_if<Value::Array>(&input.data);if(!array)fail(input,"host predicates must be an array");
    std::vector<HostConstraint> result;
    for(auto& value:*array){auto object=std::get_if<Value::Object>(&value.data);if(!object)fail(value,"host predicate must be an object");HostConstraint constraint;constraint.name=symbol(field(*object,"name"));
      for(auto& [key,v]:*object)if(key!="name"&&key!="parameters")fail(v,"unknown host predicate field: "+key,Error::Code::Unsupported);
      if(object->contains("parameters")){auto parameters=std::get_if<Value::Object>(&object->at("parameters").data);if(!parameters)fail(object->at("parameters"),"predicate parameters must be an object");constraint.parameters=*parameters;}
      result.push_back(std::move(constraint));
    }
    auto valid=validate_host_constraints(result);if(!valid)return Output::err(valid.error());return Output::ok(std::move(result));
  }catch(const Error& error){return Output::err(error);}
}
Result<int> validate_host_constraints(std::span<const HostConstraint> constraints) {
  for(auto& constraint:constraints)if(constraint.name.empty())return Result<int>::err({Error::Code::InvalidArgument,"empty host predicate name"});
  return Result<int>::ok(0);
}
Result<bool> prove_host_constraints(std::span<const HostConstraint> constraints,const Value::Object& context) {
  try {
    for(auto& constraint:constraints){if(!constraint.prove)return Result<bool>::err({Error::Code::Unsupported,"unbound target predicate: "+constraint.name});auto proved=constraint.prove(context,constraint.parameters);if(!proved)return Result<bool>::err({proved.error().code,"predicate "+constraint.name+": "+proved.error().message});if(!proved.value())return Result<bool>::ok(false);}
    return Result<bool>::ok(true);
  }catch(const Error& error){return Result<bool>::err(error);}catch(const std::bad_alloc&){return Result<bool>::err({Error::Code::ResourceLimit,"predicate allocation failed"});}catch(const std::exception& error){return Result<bool>::err({Error::Code::Internal,error.what()});}catch(...){return Result<bool>::err({Error::Code::Internal,"host predicate exception"});}
}
Value::Object selection_constraints_metadata(std::span<const OperandConstraint> constraints,std::span<const HostConstraint> host) {
  auto result=operand_constraints_metadata(constraints);Value::Array predicates;for(auto& p:host)predicates.emplace_back(Value::Object{{"name",Value(p.name)},{"parameters",Value(p.parameters)}});if(!host.empty())result["predicates"]=Value(std::move(predicates));return result;
}
Value operand_metadata(const OperandMetadata& metadata) {
  Value::Array strings;for(auto& s:metadata.strings)strings.emplace_back(Value::Object{{"index",Value(uint64_t(s.index))},{"value",Value(s.value)}});
  return Value(Value::Object{{"strings",Value(std::move(strings))},{"properties",Value(metadata.properties)}});
}
Result<OperandMetadata> load_operand_metadata(const Value& value) {
  try {
    auto object=std::get_if<Value::Object>(&value.data);if(!object)fail(value,"operand metadata must be an object");for(auto& [key,v]:*object)if(key!="strings"&&key!="properties")fail(v,"unknown operand metadata field: "+key);
    OperandMetadata result;auto properties=std::get_if<Value::Object>(&field(*object,"properties").data);if(!properties)fail(value,"operand properties must be an object");result.properties=*properties;
    auto strings=std::get_if<Value::Array>(&field(*object,"strings").data);if(!strings)fail(value,"string arguments must be an array");std::set<uint32_t> indices;
    for(auto& entry:*strings){auto s=std::get_if<Value::Object>(&entry.data);if(!s||s->size()!=2||!s->contains("index")||!s->contains("value"))fail(entry,"string argument needs index and value");auto& n=s->at("index");uint64_t index;if(auto u=std::get_if<uint64_t>(&n.data))index=*u;else if(auto i=std::get_if<int64_t>(&n.data);i&&*i>=0)index=uint64_t(*i);else fail(n,"string argument index must be unsigned");auto text=std::get_if<std::string>(&s->at("value").data);if(index>UINT32_MAX||!indices.insert(uint32_t(index)).second||!text||text->find('\0')!=std::string::npos)fail(entry,"invalid string argument");result.strings.push_back({uint32_t(index),*text});}
    std::sort(result.strings.begin(),result.strings.end(),[](auto& a,auto& b){return a.index<b.index;});return Result<OperandMetadata>::ok(std::move(result));
  }catch(const Error& error){return Result<OperandMetadata>::err(error);}
}
}
