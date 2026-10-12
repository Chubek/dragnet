#pragma once
#include "bin2bin.hpp"

namespace limestone::bin2bin {
// Machine-derived facts are always retained independently of plugin output.
struct DecompilationFacts {
  std::string architecture, version, description, execution_domain, state_model;
  uint64_t address=0;
  std::vector<uint8_t> bytes;
  ControlFlowGraph cfg;
  std::vector<LiftedInstruction> semantics;
};
struct DecompilerOutput {
  std::string language, text;
  // Provider/model/rule version for supplemental interpretations, including LLMs.
  std::string attribution;
};
struct DecompilerPlugin {
  std::string identity, language;
  std::function<Result<DecompilerOutput>(const DecompilationFacts&)> apply;
};
struct Decompilation { DecompilationFacts facts; std::vector<DecompilerOutput> interpretations; };
struct DecompilationLimits { size_t bytes=64*1024*1024, instructions=1048576, output_bytes=16*1024*1024, plugins=64; };
// Plugins run synchronously over owning immutable facts. They may call external
// services through their host adapter; their text never changes decoded facts.
Result<Decompilation> decompile_with_plugins(const Architecture&,std::span<const uint8_t>,
  std::span<const DecompilerPlugin>,uint64_t address=0,const DecompilationLimits& = {});
}
