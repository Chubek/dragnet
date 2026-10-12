#pragma once
#include "../limestone/foundation.hpp"
#include "../schedrow/schedrow.hpp"
#include "../metacode/operand_constraints.hpp"
namespace limestone::unisel {
using NodeId=uint32_t; using PatternId=uint32_t;
struct Value { NodeId id; std::string type; };
struct Node {
  NodeId id; std::string op; std::vector<NodeId> inputs; std::optional<int64_t> constant;
   std::string type; uint32_t block=0; bool required=true, side_effect=false;
   bool produces_value=true;
   std::string register_class, origin;
   std::optional<schedrow::MemoryAccess> access;
    bool call=false, terminator=false, may_trap=false;
    std::vector<uint32_t> block_targets;
     schedrow::ControlFlow control=schedrow::ControlFlow::None;
     metacode::OperandMetadata metadata;
};
struct PatternTree {
  // Empty op is a boundary operand; binding enforces repeated operand identity.
  std::string op, binding, type;
  std::vector<PatternTree> inputs;
  std::optional<std::pair<int64_t,int64_t>> immediate;
  std::string register_class;
};
struct Pattern {
  // Flat operands are concrete source types; empty strings and "v" are untyped
  // value boundaries. Structured trees express richer operand constraints.
  PatternId id; std::string name, root_op, instruction; std::vector<std::string> operands; int cost=1;
  std::optional<PatternTree> tree;
  bool supports_side_effects=false;
   std::string origin;
    std::vector<metacode::OperandConstraint> constraints;
     std::vector<metacode::HostConstraint> host_constraints;
     // Resulting memory summary is justified by the named host proof over all
     // covered accesses. Multiple source accesses cannot be merged implicitly.
     std::optional<schedrow::MemoryAccess> fused_memory;
};
struct Candidate {
  PatternId pattern; NodeId root; std::vector<NodeId> covered; int cost; std::string reason;
  std::vector<NodeId> inputs, outputs;
};
struct Selection { std::vector<Candidate> selected; int cost=0; };
struct Program { std::vector<Node> nodes; std::vector<NodeId> outputs; std::vector<schedrow::Dependency> dependencies; std::vector<schedrow::BasicBlock> blocks; uint32_t entry=0; };
// Normalize flat and structured patterns to the same typed matching contract.
PatternTree pattern_tree(const Pattern&);
// Cross-block values must dominate their uses. CFG cycles are independent of
// the acyclic SSA value graph; phi elimination belongs to an explicit adapter.
Result<int> validate_cfg(const Program&);
// Establish observable effect order before pattern fusion or optimization.
Result<Program> prepare(const Program&);
// Signed, 1-based literals. This inspectable model contains no vendor types.
struct ConstraintModel { std::vector<Candidate> candidates; std::vector<std::vector<int32_t>> clauses; };
std::vector<Candidate> match(const Program&, const std::vector<Pattern>&);
Result<std::vector<Candidate>> match_checked(const Program&,const std::vector<Pattern>&);
Result<int> validate(const Program&, const std::vector<Pattern>&);
Result<ConstraintModel> build_model(const Program&, const std::vector<Pattern>&);
Result<Selection> solve(const Program&, const std::vector<Pattern>&);
Result<Selection> solve_greedy(const Program&, const std::vector<Pattern>&);
Result<schedrow::Region> emit_scheduler(const Program&, const std::vector<Pattern>&, const Selection&);
}
