#pragma once
#include "syntax.hpp"
#include "metacode/json.hpp"
#include <charconv>
#include <map>
#include <set>

// Shared syntax-to-model utilities. Algorithm libraries need not depend on the
// parser: their textual adapters are separate CMake targets.
namespace limestone::syntax::semantic {
using Value=metacode::Value;
using Object=Value::Object;
[[noreturn]] inline void fail(std::string message,Error::Code code=Error::Code::InvalidArgument) {
  throw Error{code,std::move(message)};
}
template<class Node> [[noreturn]] void fail(const Node& node,std::string message,Error::Code code=Error::Code::InvalidArgument) {
  const auto& s=node.source;
  fail(s.file+":"+std::to_string(s.begin.line)+":"+std::to_string(s.begin.column)+": "+message,code);
}
inline std::string unquote(std::string_view token) {
  if(!token.starts_with('"'))return std::string(token);
  auto parsed=metacode::parse_json(token);
  if(!parsed||!std::holds_alternative<std::string>(parsed.value().data))fail("invalid quoted metadata string",Error::Code::Parse);
  return std::get<std::string>(parsed.value().data);
}
template<class Node> std::string spelling(const Node& node) {
  if constexpr(requires{node.value.index();})return std::visit([](const auto& p){return spelling(*p);},node.value);
  else return unquote(node.value);
}
inline uint64_t wide(std::string_view text) {
  uint64_t value=0;int base=10;
  if(text.starts_with("0x")){base=16;text.remove_prefix(2);}
  auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value,base);
  if(error!=std::errc{}||end!=text.data()+text.size())fail("expected unsigned integer: "+std::string(text));
  return value;
}
inline uint32_t number(std::string_view text) {
  auto n=wide(text);if(n>UINT32_MAX)fail("integer exceeds 32 bits");return uint32_t(n);
}
inline int64_t integer(std::string_view text) {
  if(text.starts_with("0x")){auto n=wide(text);if(n>INT64_MAX)fail("integer exceeds signed 64 bits");return int64_t(n);}
  int64_t value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{}||end!=text.data()+text.size())fail("expected signed integer: "+std::string(text));return value;
}
inline int small_integer(std::string_view text) {
  auto n=integer(text);if(n<INT32_MIN||n>INT32_MAX)fail("integer exceeds signed 32 bits");return int(n);
}
inline bool boolean(std::string_view text) {
  if(text=="true")return true;if(text=="false")return false;fail("expected Boolean: "+std::string(text));
}
inline Value token(std::string_view text) {
  if(text.starts_with('"'))return Value(unquote(text));
  if(text=="true"||text=="false")return Value(boolean(text));
  if(!text.empty()&&((text.front()>='0'&&text.front()<='9')||(text.front()=='-'&&text.size()>1&&text[1]>='0'&&text[1]<='9'))) {
    if(!text.starts_with("0x")&&text.find_first_of(".eE")!=std::string_view::npos)return Value(std::string(text));
    if(text.starts_with('-'))return Value(integer(text));
    auto n=wide(text);return n<=INT64_MAX?Value(int64_t(n)):Value(n);
  }
  return Value(std::string(text));
}
template<class Node> std::string sexpr(const Node& node) {
  if constexpr(requires{node.value.index();})return std::visit([](const auto& p){return sexpr(*p);},node.value);
  else if constexpr(requires{node.value;})return node.value;
  else if constexpr(requires{node.items;}){std::string text="(";for(auto& n:node.items){if(text.size()>1)text+=' ';text+=sexpr(*n);}return text+")";}
  else {fail(node,"unsupported semantic expression",Error::Code::Unsupported);}
}
template<class Node> Value metadata(const Node&);
template<class Node> Value attribute(const Node& node) {
  return node.value_data?metadata(*node.value_data):metadata(*node.section);
}
template<class Attributes> Object attributes(const Attributes& attrs) {
  Object out;for(auto& a:attrs)if(!out.emplace(spelling(*a->name),attribute(*a)).second)fail(*a,"duplicate attribute",Error::Code::Conflict);return out;
}
template<class Node> Value metadata(const Node& node) {
  Value result;
  if constexpr(requires{node.value.index();})result=std::visit([](const auto& p){return metadata(*p);},node.value);
  else if constexpr(requires{node.value;})result=token(node.value);
  else if constexpr(requires{node.lower;node.upper;})result=Value(Value::Array{token(node.lower->value),token(node.upper->value)});
  else if constexpr(requires{node.items;}) {
    using Kind=decltype(node.kind());
    if constexpr(requires{Kind::SExpr;})if(node.kind()==Kind::SExpr)return Value(sexpr(node));
    Value::Array values;for(auto& n:node.items)values.push_back(metadata(*n));result=Value(std::move(values));
  }else if constexpr(requires{node.attributes;})result=Value(attributes(node.attributes));
  else if constexpr(requires{node.entries;}) {
    Object values;
    for(auto& entry:node.entries)std::visit([&](const auto& p){
      std::string key;Value value;
      if constexpr(requires{p->section;}){key=spelling(*p->name);value=attribute(*p);}
      else if constexpr(requires{p->reference;}){key=spelling(*p->reference);value=metadata(*p->value_data);}
      else {key=spelling(*p->name);value=Value(true);}
      if(!values.emplace(key,std::move(value)).second)fail(*p,"duplicate object entry: "+key,Error::Code::Conflict);
    },entry->value);
    result=Value(std::move(values));
  }else fail(node,"unsupported metadata syntax",Error::Code::Unsupported);
  result.source={node.source.file,node.source.begin.offset,uint32_t(node.source.begin.line),uint32_t(node.source.begin.column)};
  return result;
}
inline const Object& object(const Value& v) {
  auto p=std::get_if<Object>(&v.data);if(!p)fail("expected metadata object");return *p;
}
inline const Value::Array& array(const Value& v) {
  auto p=std::get_if<Value::Array>(&v.data);if(!p)fail("expected metadata array");return *p;
}
inline const Value& required(const Object& o,const std::string& key) {
  auto it=o.find(key);if(it==o.end())fail("missing field: "+key);return it->second;
}
inline std::string text(const Object& o,const std::string& key,std::string fallback={}) {
  auto it=o.find(key);return it==o.end()?std::move(fallback):it->second.text();
}
inline std::string quote(const std::string& text) {
  auto result=metacode::print_json(Value(text));if(!result)throw result.error();return result.value();
}
inline std::map<std::string,Value> ordered(const Object& object) {return {object.begin(),object.end()};}
inline std::string print(const Value& value) {
  if(auto p=std::get_if<std::string>(&value.data))return quote(*p);
  if(auto p=std::get_if<Object>(&value.data)){std::string out="{";for(auto& [key,v]:ordered(*p))out+=' '+quote(key)+" = "+print(v)+";";return out+" }";}
  if(auto p=std::get_if<Value::Array>(&value.data)){std::string out="[";for(auto& v:*p){if(out.size()>1)out+=", ";out+=print(v);}return out+"]";}
  return value.text();
}
// Reserve numeric references first, then assign named references in lexical
// order. %7 and %name can coexist without an allocation-order collision.
struct Names {
  std::map<std::string,uint32_t> ids;
  void assign(const std::set<std::string>& names) {
    std::set<uint32_t> used;
    for(auto& name:names){std::string_view digits=name;if(digits.starts_with('%')||digits.starts_with('$'))digits.remove_prefix(1);if(!digits.empty()&&digits.find_first_not_of("0123456789")==std::string_view::npos){auto n=number(digits);if(!used.insert(n).second)fail("duplicate numeric identity: "+name,Error::Code::Conflict);ids[name]=n;}}
    uint64_t next=0;for(auto& name:names)if(!ids.contains(name)){while(next<=UINT32_MAX&&used.contains(uint32_t(next)))++next;if(next>UINT32_MAX)fail("reference identity exhaustion",Error::Code::ResourceLimit);ids[name]=uint32_t(next);used.insert(uint32_t(next++));}
  }
  uint32_t at(const std::string& name) const {
    auto it=ids.find(name);if(it==ids.end())fail("unknown reference: "+name,Error::Code::NotFound);return it->second;
  }
};
}
