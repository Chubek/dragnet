#pragma once
#include "text.hpp"
#include "unisel/umd.hpp"

namespace limestone::limeburg {
// Inventory semantics are instruction-scoped. Only an explicit selection_tree
// declares a computation interchangeable with a machine-independent operation.
struct InstructionCoverage {
  std::string instruction, mode, reason;
  std::optional<RuleId> rule;
  bool produces_value=false, side_effect=false, call=false, terminator=false, may_trap=false;
  std::string register_class;
};
struct InfobankSpec {
  unisel::MachineDescription machine; // Owning normalized metadata/provenance.
  RuleDocument document;
  std::vector<InstructionCoverage> coverage;
};
// Normalize representable single-result/effect instruction semantics, preserving
// literal tags and nested expressions. Missing immediate contracts and multiple
// results are recorded in coverage rather than guessed. Malformed input fails.
Result<InfobankSpec> from_infobank(const metacode::Architecture&);
Result<std::string> print_infobank_spec(const InfobankSpec&);
}
