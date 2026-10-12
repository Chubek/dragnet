#pragma once
#include "../limestone/foundation.hpp"
#include <filesystem>

namespace limestone::tunah {
struct SourceLocation {
  std::string file;
  size_t line=1, column=1, offset=0;
  // Coordinates refer to the original source or a macro's invocation. Expanded
  // byte coordinates are retained separately when EkippX changed the input.
  bool expanded=false;
  std::optional<size_t> expanded_offset;
  bool operator==(const SourceLocation&) const = default;
};
struct Term {
  std::string op;
  std::vector<uint32_t> children;
  std::optional<int64_t> constant;
  std::vector<Term> arguments;
  SourceLocation location;
  bool application=false;
};
struct Condition { std::string predicate; std::vector<Term> arguments; SourceLocation location; };
struct RewriteRule { std::string name; Term lhs, rhs; std::vector<Condition> conditions; SourceLocation location; };
// Predicates inspect equivalent representatives of matched e-classes. A host
// analysis must return true only when it can prove the rule's precondition.
using Predicate = std::function<Result<bool>(std::span<const Term>)>;
struct CostModel {
  size_t literal=1;
  // Local operator costs, excluding children. Unspecified operators cost 1+arity.
  std::unordered_map<std::string,size_t> operators;
};
struct Limits {
  size_t iterations=20, nodes=10000, classes=10000;
  uint64_t time_ms=0;
  std::function<bool()> cancelled;
  bool trace=true;
};
struct DerivationNode {
  uint32_t id=0, eclass=0;
  std::string op, rule;
  std::optional<int64_t> constant;
  std::vector<uint32_t> children;
  SourceLocation location;
  std::vector<SourceLocation> origins;
};
struct DerivationStep {
  uint32_t lhs=0, rhs=0, result=0;
  std::string rule; // Empty for a congruence merge.
  std::vector<std::pair<std::string,uint32_t>> bindings;
  SourceLocation location;
  // Congruence witnesses refer to immutable insertion facts; earlier equality
  // steps canonicalize their child classes to justify the congruence merge.
  std::optional<uint32_t> lhs_node,rhs_node;
};
struct SaturationResult {
  size_t rewrites=0; bool saturated=false;
  std::string expression; size_t cost=0, nodes=0, iterations=0;
  bool limit_reached=false;
  std::vector<std::string> trace;
  Term term;
  size_t classes=0;
  // Ordered insertion and equality facts; enabled by Limits::trace.
  std::vector<DerivationNode> derivation_nodes;
  std::vector<DerivationStep> derivation_steps;
  bool graph_discarded=false;
  std::optional<uint32_t> root_class,extracted_class;
};
std::string print_derivation(const SaturationResult&);
Result<Term> parse_term(std::string_view expression, std::string_view source="<tunah>");
std::string format_term(const Term&);

class Session {
  struct PreparedRules;
  struct PredicateInfo { size_t arity; Predicate evaluate; };
  std::vector<RewriteRule> rules_;
  std::unordered_map<std::string,size_t> operators_;
  std::unordered_map<std::string,PredicateInfo> predicates_;
  std::shared_ptr<const PreparedRules> prepared_;
  void validate_rule(const RewriteRule&, const std::unordered_map<std::string,size_t>&) const;
  std::shared_ptr<const PreparedRules> prepare_rules(const std::vector<RewriteRule>&,
    const std::unordered_map<std::string,size_t>&) const;
 public:
  void add_rule(RewriteRule rule) { rules_.push_back(std::move(rule)); prepared_.reset(); }
  const std::vector<RewriteRule>& rules() const { return rules_; }
  const std::unordered_map<std::string,size_t>& operators() const { return operators_; }
  Result<int> define_operator(std::string name, size_t arity);
  Result<int> define_predicate(std::string name, size_t arity, Predicate);
  // Loads are transactional, including (operator name arity) declarations.
  Result<int> load_rules(std::string_view specification, std::string_view source="<tunah>");
  Result<int> load_rules_file(const std::filesystem::path&);
  // Rules are caller-supplied equivalences; operators with operands need signatures.
  Result<SaturationResult> saturate(std::string_view root, Limits = {}, const CostModel& = {}) const;
  Result<SaturationResult> saturate(const Term& root, Limits = {}, const CostModel& = {}) const;
};
}
