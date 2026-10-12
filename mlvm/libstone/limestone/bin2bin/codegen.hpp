#pragma once
#include "bin2bin.hpp"
#include "regtl/regtl.hpp"
#include "schedrow/schedrow.hpp"

namespace limestone::bin2bin {
struct OperandBinding {
  enum class Kind { Definition, Use, Immediate, BlockTarget, FixedDefinition, FixedUse } kind;
  size_t index=0;
  std::string physical_register;
};
using EncodingBindings=std::map<std::string,std::map<std::string,OperandBinding>>;
Result<std::map<std::string,OperandBinding>> encoding_operands(const metacode::Value::Object&,std::string_view origin={});
// Decode the explicit per-operation encoding_operands contract once at ingress.
Result<EncodingBindings> encoding_bindings(const metacode::Architecture&);
// Encoded register operands use the allocated physical name, never a virtual ID.
// Spills require a target materialization pass before this adapter can run.
Result<std::vector<uint8_t>> encode_region(const Architecture&,const EncodingBindings&,
  const std::map<uint32_t,std::string>& physical_names,const schedrow::Region&,
  std::span<const uint32_t> order,const std::optional<regtl::Allocation>&,uint64_t address=0);
}
