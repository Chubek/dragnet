#pragma once
#include "regtl.hpp"
#include "metacode/metacode.hpp"
#include <map>

namespace limestone::regtl {
struct TextFunction {
  std::string name;
  Function function;
  std::map<uint32_t,metacode::Value::Object> instruction_metadata,block_metadata;
};
struct StorageSlot { std::string name; uint32_t id,size,alignment; metacode::Value::Object metadata; };
struct AllocationUnit {
  std::string name;
  Program problem;
  std::vector<TextFunction> functions;
  std::vector<StorageSlot> slots;
  std::map<std::string,uint32_t> physical_names,virtual_names;
  std::map<VReg,metacode::Value::Object> value_metadata;
  metacode::Value::Object metadata;
  PbqpOptions pbqp;
};
Result<std::vector<AllocationUnit>> load_regtl(std::string_view,std::string_view file="<regtl>");
Result<std::string> print_regtl(std::span<const AllocationUnit>);
}
