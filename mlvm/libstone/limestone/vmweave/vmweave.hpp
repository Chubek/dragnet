#pragma once
#include "limestone/foundation.hpp"
#include <map>

namespace limestone::vmweave {
struct Location { std::string file; uint32_t line = 0; };
struct Field { std::string name, type; uint32_t count = 0; };
struct Instruction {
  std::string name, semantics, stack_effect = "( -- )", flow = "next";
  std::optional<uint32_t> opcode;
  std::vector<std::string> operands;
  Location source;
};
struct Rewrite {
  // Explicit equivalence assertions supplied by the VM author. Operand-bearing
  // and control-flow instructions are deliberately excluded from this facility.
  std::vector<std::string> from, to;
};
struct Specification {
  std::string name, execution = "switch", cell = "intptr_t", allocator = "custom";
  uint32_t stack_capacity = 1024, frame_capacity = 64, ipc_capacity = 64;
  bool hooks = false;
  std::vector<Field> fields;
  std::vector<Instruction> instructions;
  std::vector<std::string> components;
  std::map<std::string, std::string> subsystem_c;
  std::vector<Rewrite> rewrites;
  Location source;
};
// STK-00 v1 contains stack-effect words with an explicit foreign-C primitive.
// C is retained losslessly: it is not guessed into machine operations.
struct Word {
  std::string name, effect, flow, c_body;
  uint32_t opcode = 0;
  std::vector<std::string> operands;
  Location source;
};
struct Module {
  Specification configuration;
  std::vector<Word> words;
};
struct Artifacts { std::map<std::string, std::string> files; };
Result<Specification> load_lua(std::string_view text, std::string_view file = "<lua>");
Result<Specification> load_file(const std::string& file);
Result<Specification> validate(Specification);
Result<Module> lower(Specification);
Result<std::string> print_stk(const Module&);
Result<Module> parse_stk(std::string_view text, std::string_view file = "<stk-00>");
Result<std::string> emit_c(const Module&);
Result<Artifacts> generate(const Module&);
const std::vector<std::string>& component_names();
// Target adapters must lower the authoritative C primitive, not reinterpret a
// mnemonic. Returned exchanges are validated by metacode/machine-ir/bridge.
class MachineIRAdapter {
public:
  virtual ~MachineIRAdapter() = default;
  virtual Result<std::string> lower(const Module&) = 0;
};
Result<std::string> emit_machineir(const Module&, MachineIRAdapter&);
} // namespace limestone::vmweave
