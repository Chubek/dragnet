#pragma once
#include "../limestone/foundation.hpp"
#include "../schedrow/schedrow.hpp"
#include "../metacode/operand_constraints.hpp"
#include <map>
namespace limestone::limeburg {
using NodeId=uint32_t; using RuleId=uint32_t;
using NonterminalId=uint32_t;
struct Node { NodeId id; std::string op, type; std::vector<NodeId> children; int64_t imm=0; bool has_imm=false; bool required=true, produces_value=true, side_effect=false; std::optional<schedrow::MemoryAccess> access; bool call=false, terminator=false, may_trap=false; std::string origin, register_class;
  // A known forest value remains a register operand across the boundary.
  std::optional<int64_t> known_constant;
  metacode::OperandMetadata metadata;
};
struct Pattern {
  std::string op, nonterminal, type;
  std::vector<Pattern> children;
  std::optional<std::pair<int64_t,int64_t>> immediate;
  std::string binding, register_class;
};
struct Rule {
  RuleId id; std::string lhs, op, result; std::vector<std::string> operands; int cost=1; std::string instruction; int priority=0;
  std::string type;
  std::optional<std::pair<int64_t,int64_t>> immediate;
  std::optional<Pattern> pattern;
  bool supports_side_effects=false;
  bool external_only=false;
  std::string origin;
  std::vector<metacode::OperandConstraint> constraints;
  std::vector<metacode::HostConstraint> host_constraints;
  std::optional<schedrow::MemoryAccess> fused_memory;
};
struct RuleSet { std::vector<Rule> rules; std::unordered_map<std::string,uint32_t> nonterminals; };
struct Derivation {
  RuleId rule; int cost; std::vector<std::string> child_nt; std::vector<NodeId> children, covered;
  bool operator==(const Derivation&) const = default;
};
struct Selection { std::string root_nt; std::unordered_map<NodeId,Derivation> chosen; int cost=0; std::optional<NodeId> root; };
struct RuleAttempt {
  NodeId node; RuleId rule;
  // No cost means the rule did not match. State updates use cost, priority, ID.
  std::optional<int> cost;
  bool improves_state=false;
  std::string reason;
};
struct StateTable {
  NodeId root;
  std::map<NodeId,std::map<NonterminalId,Derivation>> states;
  std::vector<RuleAttempt> attempts;
};
// Compute all reachable BURS states, even when the requested root nonterminal
// has no derivation. Optional attempts retain deterministic rejection reasons.
Result<StateTable> analyze(const std::vector<Node>&,NodeId,const RuleSet&,bool trace=false);
// Stable human-readable state and rule-attempt dump, with rule provenance.
Result<std::string> print_analysis(const StateTable&,const RuleSet&);
Result<Selection> select(const std::vector<Node>&, NodeId, const RuleSet&, std::string_view root);
Result<int> validate(const RuleSet&);
Result<schedrow::Region> emit_scheduler(const std::vector<Node>&, const RuleSet&, const Selection&);
}
