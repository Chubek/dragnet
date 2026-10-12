#pragma once
#include "schedrow.hpp"

namespace limestone::schedrow {
// Owning target-neutral predicate facts; no solver/allocation state is exposed.
inline metacode::Value operand_facts(uint32_t id,std::string_view opcode,std::string_view type,
    std::string_view klass,std::optional<int64_t> immediate,const metacode::OperandMetadata& metadata,
    const std::optional<MemoryAccess>& memory,bool effect,bool call,bool terminator,bool trap,std::span<const uint32_t> inputs={}) {
  using V=metacode::Value;V::Object result{{"id",V(uint64_t(id))},{"opcode",V(std::string(opcode))},{"type",V(std::string(type))},{"class",V(std::string(klass))},
    {"metadata",metacode::operand_metadata(metadata)},{"side_effect",V(effect)},{"call",V(call)},{"terminator",V(terminator)},{"may_trap",V(trap)}};
  if(immediate)result["immediate"]=V(*immediate);
  V::Array operands;for(auto input:inputs)operands.emplace_back(uint64_t(input));result["inputs"]=V(std::move(operands));
  if(memory){V::Array aliases;for(auto id:memory->alias_sets)aliases.emplace_back(uint64_t(id));result["memory"]=V(V::Object{{"read",V(memory->read)},{"write",V(memory->write)},{"volatile",V(memory->volatile_access)},{"atomic",V(memory->atomic)},
    {"ordering",V(uint64_t(memory->ordering))},{"address_space",V(memory->address_space)},{"alias_sets",V(std::move(aliases))},{"size",V(uint64_t(memory->size))},{"alignment",V(uint64_t(memory->alignment))}});}
  return V(std::move(result));
}
}
