#pragma once
#include "tunah.hpp"
#include "unisel/unisel.hpp"

namespace limestone::tunah {
struct GraphOperator {
  std::string term_operator, type;
  size_t arity;
  bool commutative=false;
};
struct GraphAdapterOptions {
  // Each registered symbol has one concrete type and an explicit pure semantics.
  std::unordered_map<std::string,GraphOperator> operators;
  std::string constant_opcode="const";
  size_t reconstructed_nodes=10000;
  Limits limits;
  CostModel costs;
};
struct GraphOptimization {
  unisel::Program program;
  std::unordered_map<unisel::NodeId,unisel::NodeId> values;
  size_t rewrites=0;
  bool limit_reached=false;
};
// Transactional, typed bidirectional adapter. Effectful nodes and semantic edges
// are pinned. Shared inputs remain references instead of duplicated computations.
Result<GraphOptimization> optimize_graph(const unisel::Program&, const Session&, const GraphAdapterOptions&);
}
