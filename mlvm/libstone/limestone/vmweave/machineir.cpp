#include "internal.hpp"
#include "metacode/machine-ir/bridge.hpp"
namespace limestone::vmweave {
Result<std::string> emit_machineir(const Module& m,MachineIRAdapter& adapter) {
  auto checked=checked_module(m); if(!checked) return Result<std::string>::err(checked.error());
  auto lowered=adapter.lower(checked.value()); if(!lowered) return lowered;
  auto exchange=machineir_bridge::deserialize(lowered.value(),"<VMWeave MachineIR adapter>");
  if(!exchange) return Result<std::string>::err(exchange.error());
  return machineir_bridge::serialize(exchange.value());
}
}
