#pragma once
#include "schedrow.hpp"

namespace limestone::schedrow::metadata {
inline Result<int> validate_memory_access(const MemoryAccess& memory) {
  if(memory.ordering<MemoryOrdering::Relaxed||memory.ordering>MemoryOrdering::Sequential)
    return Result<int>::err({Error::Code::InvalidArgument,"unknown memory ordering"});
  if(memory.alignment&&(memory.alignment&(memory.alignment-1)))
    return Result<int>::err({Error::Code::InvalidArgument,"memory alignment must be a power of two"});
  if(memory.address_space.find('\0')!=std::string::npos)
    return Result<int>::err({Error::Code::InvalidArgument,"invalid memory address space"});
  return Result<int>::ok(0);
}
inline metacode::Value value(const MemoryAccess& memory) {
  using V=metacode::Value;V::Array aliases;for(auto alias:memory.alias_sets)aliases.emplace_back(uint64_t(alias));
  static constexpr const char* names[]={"relaxed","acquire","release","acq_rel","seq_cst"};
  auto valid=validate_memory_access(memory);if(!valid)throw valid.error();
  return V(V::Object{{"read",V(memory.read)},{"write",V(memory.write)},{"volatile",V(memory.volatile_access)},
    {"atomic",V(memory.atomic)},{"ordering",V(std::string(names[size_t(memory.ordering)]))},{"address_space",V(memory.address_space)},
    {"alias_sets",V(std::move(aliases))},{"size",V(uint64_t(memory.size))},{"alignment",V(uint64_t(memory.alignment))}});
}
inline Result<MemoryAccess> load_memory_access(const metacode::Value& input) {
  using V=metacode::Value;
  try {
    auto object=std::get_if<V::Object>(&input.data);if(!object)throw Error{Error::Code::InvalidArgument,"memory contract must be an object"};
    auto flag=[&](const char* name){auto it=object->find(name);if(it==object->end())return false;auto value=std::get_if<bool>(&it->second.data);if(!value)throw Error{Error::Code::InvalidArgument,std::string("memory flag must be Boolean: ")+name};return *value;};
    auto text=[&](const char* name){auto it=object->find(name);if(it==object->end())return std::string{};auto value=std::get_if<std::string>(&it->second.data);if(!value||value->find('\0')!=value->npos)throw Error{Error::Code::InvalidArgument,std::string("invalid memory string: ")+name};return *value;};
    auto number=[](const V& value){uint64_t n;if(auto p=std::get_if<uint64_t>(&value.data))n=*p;else if(auto p=std::get_if<int64_t>(&value.data);p&&*p>=0)n=uint64_t(*p);else throw Error{Error::Code::InvalidArgument,"memory quantity must be unsigned"};if(n>UINT32_MAX)throw Error{Error::Code::InvalidArgument,"memory quantity exceeds 32 bits"};return uint32_t(n);};
    MemoryAccess memory{flag("read"),flag("write"),flag("volatile"),flag("atomic")};memory.address_space=text("address_space");
    if(object->contains("size"))memory.size=number(object->at("size"));if(object->contains("alignment"))memory.alignment=number(object->at("alignment"));
    if(memory.alignment&&(memory.alignment&(memory.alignment-1)))throw Error{Error::Code::InvalidArgument,"memory alignment must be a power of two"};
    if(object->contains("alias_sets")){auto values=std::get_if<V::Array>(&object->at("alias_sets").data);if(!values)throw Error{Error::Code::InvalidArgument,"memory alias sets must be an array"};for(auto& item:*values)memory.alias_sets.push_back(number(item));}
    auto ordering=text("ordering");static constexpr std::string_view names[]={"relaxed","acquire","release","acq_rel","seq_cst"};
    if(!ordering.empty()){auto it=std::find(std::begin(names),std::end(names),ordering);if(it==std::end(names))throw Error{Error::Code::InvalidArgument,"unknown memory ordering"};memory.ordering=MemoryOrdering(it-std::begin(names));}
    for(auto& [key,v]:*object)if(key!="read"&&key!="write"&&key!="volatile"&&key!="atomic"&&key!="ordering"&&key!="address_space"&&key!="alias_sets"&&key!="size"&&key!="alignment")throw Error{Error::Code::Unsupported,"unknown memory contract field: "+key};
    return Result<MemoryAccess>::ok(std::move(memory));
  }catch(const Error& error){return Result<MemoryAccess>::err(error);}
}
}
