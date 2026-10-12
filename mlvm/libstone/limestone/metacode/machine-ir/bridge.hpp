#pragma once
#include "metacode/json.hpp"
#include "schedrow/schedrow.hpp"
#include "regtl/regtl.hpp"

namespace limestone::machineir_bridge {
// A versioned region/CFG exchange adapter, not a second MachineIR model.
struct ValueInfo {uint32_t id;std::string type,register_class;};
struct RegionExchange {
  std::string module="module",target,function="main";
  schedrow::Region region;
  std::vector<uint32_t> order,outputs;
  std::vector<ValueInfo> values;
  std::vector<schedrow::Scheduled> schedule;
  std::optional<regtl::Allocation> allocation;
  std::vector<regtl::SpillSlot> spill_slots;
  uint64_t frame_size=0;
};
Result<std::string> serialize(const RegionExchange&);
Result<RegionExchange> deserialize(std::string_view,std::string_view file="<machineir>");
}
