#include <limeburg/infobank.hpp>
#include <metacode/metacode.hpp>

int main() {
  auto architecture=limestone::metacode::parse_isa(R"ISA(
arch consumer {}
profile { register_classes=["G"]; }
regclass G { r0(32)=0, }
op plus { operands=rd:G, left:G, right:G; semantics="(set rd (add left right))";
  tooling={
    dataflow={uses=["left","right"];defs=["rd"];explicit_operands=["rd","left","right"];memory="none";flags_read=false;flags_written=false;};
    vmm={terminator=false;may_trap=false;atomic=false;serializing=false;control_flow="fallthrough";};
  };
}
)ISA","consumer.isa");
  if(!architecture)return 1;
  auto specification=limestone::limeburg::from_infobank(architecture.value());if(!specification)return 1;
  if(specification.value().coverage[0].mode!="inventory"||specification.value().document.rules.rules[0].instruction!="plus")return 1;
  auto text=limestone::limeburg::print_infobank_spec(specification.value());if(!text)return 1;
  auto loaded=limestone::limeburg::load_rules(text.value());if(!loaded||loaded.value().name!="consumer")return 1;
  return 0;
}
