#include <vmweave/vmweave.hpp>
#include <vmweave/native.hpp>
int main() {
  auto spec=limestone::vmweave::load_lua("local v=require('vmweave'); return v.vm {name='Installed',instructions={{name='HALT',flow='halt',semantics='vm_halt();'}}}");
  if(!spec) return 1;
  auto ir=limestone::vmweave::lower(spec.value()); if(!ir) return 2;
  auto source=limestone::vmweave::emit_c(ir.value());
  if(!source || source.value().find("Installed_HALT")==std::string::npos) return 3;
  auto code=limestone::vmweave::assemble(ir.value(),"HALT"); if(!code) return 4;
  auto native=limestone::vmweave::compile_native(ir.value(),code.value()); if(!native) return 5;
  return !native.value().image().empty() && native.value().state_size() && !native.value().machine_ir().empty() ? 0 : 6;
}
