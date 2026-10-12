#ifndef LIMESTONE_OPTIMIZATION_H
#define LIMESTONE_OPTIMIZATION_H
#include "limestone.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @file optimization.h
 * Owning Tunah C adapter. Link Limestone::optimization for term optimization
 * and Limestone::core to attach an optimizer to a pipeline target. Inputs are
 * copied; results may outlive their optimizer. All checked calls accept a NULL
 * diagnostic. Independent handles are reentrant; synchronize shared mutations.
 * Rules and pure operator declarations are host-supplied semantic equivalences.
 */
typedef struct limestone_optimizer limestone_optimizer;
typedef struct limestone_optimization limestone_optimization;
typedef struct limestone_optimization_limits {
  size_t iterations,nodes,classes,reconstructed_nodes;
  uint64_t time_ms;
  int trace;
} limestone_optimization_limits;
typedef struct limestone_optimization_info {
  size_t cost,rewrites,nodes,classes,iterations;
  int saturated,limit_reached;
} limestone_optimization_info;
/** Predicate arguments are canonical equivalent terms, borrowed for the call.
 * Write proved=1 only for a proved precondition, or 0 for unknown/inapplicable.
 * Callbacks return a status and copied diagnostic; Boolean outputs must be 0/1.
 */
typedef limestone_status (*limestone_optimizer_predicate)(const char *const *arguments,
  size_t count,int *proved,void *userdata,limestone_error *);
typedef limestone_status (*limestone_optimizer_cancellation)(void *userdata,int *cancelled,limestone_error *);
/** An optional nonthrowing release adopts userdata only after successful
 * registration. It runs once after the last session/target/active-call snapshot
 * is destroyed. Without release the host retains ownership. Predicate/cancellation
 * callbacks may mutate or destroy their source optimizer: active calls use owning
 * snapshots. A release must not destroy a handle during that handle's destructor.
 */
typedef void (*limestone_optimizer_release)(void *userdata);

void limestone_optimization_limits_default(limestone_optimization_limits *);
limestone_optimizer *limestone_optimizer_create(limestone_error *);
void limestone_optimizer_destroy(limestone_optimizer *);
limestone_status limestone_optimizer_define_operator(limestone_optimizer *,const char *name,size_t arity,limestone_error *);
/** Loads are transactional, including operator declarations and rule compilation.
 * source_name may be NULL for the default diagnostic identity. File loads retain
 * bounded EkippX preprocessing and source provenance. */
limestone_status limestone_optimizer_load_rules(limestone_optimizer *,const char *source,const char *source_name,limestone_error *);
limestone_status limestone_optimizer_load_rules_file(limestone_optimizer *,const char *path,limestone_error *);
size_t limestone_optimizer_rule_count(const limestone_optimizer *);
limestone_status limestone_optimizer_define_predicate(limestone_optimizer *,const char *name,size_t arity,
  limestone_optimizer_predicate,void *userdata,limestone_optimizer_release,limestone_error *);
limestone_status limestone_optimizer_set_literal_cost(limestone_optimizer *,size_t cost,limestone_error *);
limestone_status limestone_optimizer_set_operator_cost(limestone_optimizer *,const char *name,size_t cost,limestone_error *);
/** NULL limits restore defaults. Zero iterations is an extraction-only run;
 * node/class/reconstruction limits must be positive. Deadlines/cancellation are
 * cooperative and preserve equivalent extraction on a successful budget stop. */
limestone_status limestone_optimizer_set_limits(limestone_optimizer *,const limestone_optimization_limits *,limestone_error *);
/** NULL callback clears cancellation; userdata/release must then also be NULL. */
limestone_status limestone_optimizer_set_cancellation(limestone_optimizer *,limestone_optimizer_cancellation,
  void *userdata,limestone_optimizer_release,limestone_error *);
/** Declare a pure, concrete, single-type source opcode for the graph adapter.
 * term_operator must already have a signature; commutative is a host-proved
 * binary equivalence. Effectful graph nodes and semantic edge endpoints stay
 * pinned. Shared inputs, CFGs, outputs and live-outs are preserved. */
limestone_status limestone_optimizer_define_graph_operator(limestone_optimizer *,const char *opcode,
  const char *term_operator,const char *type,int commutative,limestone_error *);
limestone_status limestone_optimizer_set_constant_opcode(limestone_optimizer *,const char *opcode,limestone_error *);
/** Snapshot all rules, callbacks, costs, limits and typed graph declarations into
 * the target. Later source mutations/destruction do not alter it. NULL removes
 * the target optimizer. Pipeline optimize=0 skips the attached optimizer. */
limestone_status limestone_target_set_optimizer(limestone_target *,const limestone_optimizer *,limestone_error *);

limestone_optimization *limestone_optimizer_saturate(const limestone_optimizer *,const char *expression,
  const char *source_name,limestone_error *);
void limestone_optimization_destroy(limestone_optimization *);
/** Borrowed output strings remain valid until result destruction. */
const char *limestone_optimization_expression(const limestone_optimization *);
limestone_status limestone_optimization_get_info(const limestone_optimization *,limestone_optimization_info *,limestone_error *);
size_t limestone_optimization_trace_count(const limestone_optimization *);
const char *limestone_optimization_trace(const limestone_optimization *,size_t index);
/* Borrow the deterministic e-node insertion/equality derivation. Empty when
 * tracing was disabled; retained independently of the source optimizer. */
const char *limestone_optimization_derivation(const limestone_optimization *);
/** True when an interrupted vendor operation discarded its partial graph and
 * returned the original checked term. Rewrite counts retain committed unions. */
int limestone_optimization_used_fallback(const limestone_optimization *);

/** Bin2Bin control classifications differ from scheduling IL classifications. */
typedef enum limestone_binary_control_flow
#ifdef __cplusplus
  : int
#endif
{
  LIMESTONE_BINARY_FALLTHROUGH,LIMESTONE_BINARY_BRANCH,LIMESTONE_BINARY_CONDITIONAL_BRANCH,
  LIMESTONE_BINARY_CALL,LIMESTONE_BINARY_RETURN,LIMESTONE_BINARY_INDIRECT_BRANCH,LIMESTONE_BINARY_TRAP
} limestone_binary_control_flow;
/** The original supported instruction's semantics and control boundary.
 * View storage and strings are borrowed only for the legality callback. */
typedef struct limestone_binary_semantic_view {
  uint64_t address;
  const char *semantics;
  limestone_binary_control_flow control;
  int has_branch_target;
  uint64_t branch_target;
} limestone_binary_semantic_view;
/** Called at ingress and after extraction. Prove types, widths, architectural
 * state/effects and operand legality in the host's semantic vocabulary. This
 * proof validates a candidate; the registered rules establish equivalence.
 * Write proved=1 only on a proof, 0 for illegal/unknown. Status/error behavior
 * and userdata ownership follow the predicate callback contract above. */
typedef limestone_status (*limestone_binary_legality)(const limestone_binary_semantic_view *,
  const char *candidate,int *proved,void *userdata,limestone_error *);
/** Snapshot the rules, predicates, costs and limits into an immutable transform.
 * A nonempty context_identity versions host semantics, predicates and legality
 * analysis. Rule/operator/cost/budget changes enter cache identity automatically;
 * callback addresses never enter it. Failed creation does not adopt userdata.
 * Legality is mandatory. Instruction/control boundaries and branch targets stay
 * intact. Runtime callbacks may invalidate code, but cannot destroy an active
 * runtime; recursive preparation returns conflict. */
limestone_binary_transform *limestone_optimizer_binary_transform(const limestone_optimizer *,
  const char *context_identity,limestone_binary_legality,void *userdata,limestone_optimizer_release,limestone_error *);
void limestone_binary_transform_destroy(limestone_binary_transform *);
/** Borrowed immutable identity; NULL for a NULL handle. */
const char *limestone_binary_transform_identity(const limestone_binary_transform *);
/** True when time/cancellation-independent; NULL is not cacheable. */
int limestone_binary_transform_is_cacheable(const limestone_binary_transform *);

#ifdef __cplusplus
}
#endif
#endif
