#ifndef LIMESTONE_IL_H
#define LIMESTONE_IL_H
#include "limestone.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @file il.h
 * Standalone C adapters for Unisel, Limeburg, Schedrow and RegTL. Link Limestone::il.
 * Loaders copy source text and metadata. Each result owns its data and may outlive
 * its document. Borrowed strings remain valid until their handle is destroyed.
 * Independent handles are reentrant; synchronize use/destruction of shared handles.
 * Destroy functions accept NULL. Every checked operation accepts a NULL diagnostic.
 */
typedef struct limestone_burs_document limestone_burs_document;
typedef struct limestone_burs_selection limestone_burs_selection;
typedef struct limestone_burs_analysis limestone_burs_analysis;
typedef struct limestone_scheduling_document limestone_scheduling_document;
typedef struct limestone_schedule limestone_schedule;
typedef struct limestone_allocation_document limestone_allocation_document;
typedef struct limestone_assignment limestone_assignment;
typedef struct limestone_unisel_document limestone_unisel_document;
typedef struct limestone_selection_model limestone_selection_model;
typedef struct limestone_selection limestone_selection;

/** Load a UMD target and an optional source graph. Analysis requires a graph.
 * source_name may be NULL. Text includes require the C++ resolver API; file loads
 * resolve bounded relative includes with original provenance (16 MiB total,
 * 128 document occurrences, 32 include levels). Target metadata is retained. */
limestone_unisel_document *limestone_unisel_load(const char *source,const char *source_name,limestone_error *);
limestone_unisel_document *limestone_unisel_load_file(const char *path,limestone_error *);
void limestone_unisel_document_destroy(limestone_unisel_document *);
limestone_status limestone_unisel_set_selection_predicate(limestone_unisel_document *,const char *name,
  const limestone_selection_predicate *,limestone_error *);
/** Prepare effects and construct the vendor-independent selection model.
 * Uncovered operations remain inspectable as empty coverage clauses. The model
 * owns source/pattern snapshots independently of its document. */
limestone_selection_model *limestone_unisel_analyze(const limestone_unisel_document *,limestone_error *);
void limestone_selection_model_destroy(limestone_selection_model *);
/** Borrow candidate metadata until the model or result is destroyed. Index order
 * is deterministic: models use root/cost/pattern, results use emission order. */
typedef struct limestone_match_info {
  uint32_t pattern,root;
  uint64_t cost;
  const char *name,*opcode,*origin,*reason;
  size_t covered_count,input_count,output_count;
} limestone_match_info;
typedef enum limestone_match_values
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_MATCH_COVERED,LIMESTONE_MATCH_INPUT,LIMESTONE_MATCH_OUTPUT } limestone_match_values;
size_t limestone_selection_model_candidate_count(const limestone_selection_model *);
limestone_status limestone_selection_model_candidate(const limestone_selection_model *,size_t candidate,limestone_match_info *,limestone_error *);
limestone_status limestone_selection_model_value(const limestone_selection_model *,size_t candidate,
  limestone_match_values,size_t index,uint32_t *value,limestone_error *);
/** Signed 1-based literals refer to model candidates; positive selects, negative
 * excludes. Empty clauses express missing coverage. Clauses do not expose Satie
 * implementation types or auxiliary objective variables. */
size_t limestone_selection_model_clause_count(const limestone_selection_model *);
size_t limestone_selection_model_literal_count(const limestone_selection_model *,size_t clause);
limestone_status limestone_selection_model_literal(const limestone_selection_model *,size_t clause,size_t index,
  int32_t *literal,limestone_error *);
const char *limestone_selection_model_text(const limestone_selection_model *);
/** Global selection is minimum-cost exact covering; greedy is deterministic and
 * may fail even when global covering is feasible. BURS uses its separate API.
 * A result owns its selected matches and Scheduler IR independently of the model.
 * Scheduling timing, allocation and encoding remain downstream contracts. */
limestone_selection *limestone_selection_run(const limestone_selection_model *,limestone_selector,limestone_error *);
void limestone_selection_destroy(limestone_selection *);
uint64_t limestone_selection_cost(const limestone_selection *);
size_t limestone_selection_count(const limestone_selection *);
limestone_status limestone_selection_candidate(const limestone_selection *,size_t candidate,limestone_match_info *,limestone_error *);
limestone_status limestone_selection_value(const limestone_selection *,size_t candidate,
  limestone_match_values,size_t index,uint32_t *value,limestone_error *);
/** Canonical Schedrow handoff retaining operands, effects, CFG and provenance. */
const char *limestone_selection_text(const limestone_selection *);

/** Load rules and input trees; source_name may be NULL for a default diagnostic name. */
limestone_burs_document *limestone_burs_load(const char *source,const char *source_name,limestone_error *);
/** Load a file and relative includes, retaining included source locations. The
 * source graph is bounded to 16 MiB, 128 documents and 32 include levels. */
limestone_burs_document *limestone_burs_load_file(const char *path,limestone_error *);
void limestone_burs_document_destroy(limestone_burs_document *);
limestone_status limestone_burs_set_selection_predicate(limestone_burs_document *,const char *name,
  const limestone_selection_predicate *,limestone_error *);
size_t limestone_burs_tree_count(const limestone_burs_document *);
const char *limestone_burs_tree_name(const limestone_burs_document *,size_t tree);
const char *limestone_burs_document_text(const limestone_burs_document *);
/** A least-cost state at a source node. nonterminal_name is borrowed from the
 * analysis handle; it remains valid until that handle is destroyed. */
typedef struct limestone_burs_state {
  uint32_t node,nonterminal,rule;
  uint64_t cost;
  const char *nonterminal_name;
} limestone_burs_state;
/** A traced rule attempt. cost is valid only when matched is nonzero. reason is
 * borrowed from the analysis handle. improves_state describes that attempt's
 * update, not whether it belongs to the final selected root derivation. */
typedef struct limestone_burs_attempt {
  uint32_t node,rule;
  int matched,improves_state;
  uint64_t cost;
  const char *reason;
} limestone_burs_attempt;
/** Analyze all reachable states and rule attempts, including no-derivation cases.
 * The owning result may outlive its document. Malformed trees still fail. */
limestone_burs_analysis *limestone_burs_analyze(const limestone_burs_document *,size_t tree,limestone_error *);
void limestone_burs_analysis_destroy(limestone_burs_analysis *);
size_t limestone_burs_state_count(const limestone_burs_analysis *);
limestone_status limestone_burs_state_at(const limestone_burs_analysis *,size_t,limestone_burs_state *,limestone_error *);
size_t limestone_burs_attempt_count(const limestone_burs_analysis *);
limestone_status limestone_burs_attempt_at(const limestone_burs_analysis *,size_t,limestone_burs_attempt *,limestone_error *);
/** Select the indexed tree's declared root nonterminal using BURS. */
limestone_burs_selection *limestone_burs_select(const limestone_burs_document *,size_t tree,limestone_error *);
void limestone_burs_selection_destroy(limestone_burs_selection *);
uint64_t limestone_burs_selection_cost(const limestone_burs_selection *);
size_t limestone_burs_selection_count(const limestone_burs_selection *);
const char *limestone_burs_selection_opcode(const limestone_burs_selection *,size_t instruction);
/** Canonical Schedrow handoff retaining definitions, uses, immediates and effects. */
const char *limestone_burs_selection_text(const limestone_burs_selection *);

limestone_scheduling_document *limestone_schedrow_load(const char *source,const char *source_name,limestone_error *);
void limestone_scheduling_document_destroy(limestone_scheduling_document *);
size_t limestone_scheduling_region_count(const limestone_scheduling_document *);
const char *limestone_scheduling_region_name(const limestone_scheduling_document *,size_t region);
const char *limestone_scheduling_document_text(const limestone_scheduling_document *);
/** Run and verify the indexed region. initiation_interval=0 selects list/CFG
 * scheduling; a positive interval selects modulo scheduling. A modulo result is
 * an iteration schedule and requires loop expansion before sequential emission.
 */
limestone_schedule *limestone_schedule_run(const limestone_scheduling_document *,size_t region,
  uint32_t initiation_interval,limestone_error *);
void limestone_schedule_destroy(limestone_schedule *);
size_t limestone_schedule_count(const limestone_schedule *);
typedef struct limestone_issue { uint32_t instruction,cycle;int has_slot;uint32_t slot; } limestone_issue;
limestone_status limestone_schedule_issue(const limestone_schedule *,size_t index,limestone_issue *,limestone_error *);
/** All consumed issue slots, including the primary slot. */
size_t limestone_schedule_slot_count(const limestone_schedule *,size_t index);
limestone_status limestone_schedule_slot(const limestone_schedule *,size_t index,size_t slot,uint32_t *value,limestone_error *);
size_t limestone_schedule_resource_count(const limestone_schedule *,size_t index);
const char *limestone_schedule_resource(const limestone_schedule *,size_t index,size_t reservation);
/** Borrow a diagnostic listing of the region, dependencies and issue assignments. */
const char *limestone_schedule_text(const limestone_schedule *);
typedef enum limestone_group_kind
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_GROUP_ORDERED, LIMESTONE_GROUP_ADJACENT, LIMESTONE_GROUP_SAME_CYCLE,
  LIMESTONE_GROUP_BUNDLE, LIMESTONE_GROUP_ATOMIC, LIMESTONE_GROUP_FUSION, LIMESTONE_GROUP_PAIR } limestone_group_kind;
/** Names/provenance/patterns are borrowed until schedule destruction. */
typedef struct limestone_group_info {
  uint32_t id; limestone_group_kind kind;
  const char *name,*origin,*pattern;
  int benefit; uint32_t issue_width; size_t member_count;
} limestone_group_info;
size_t limestone_schedule_group_count(const limestone_schedule *);
limestone_status limestone_schedule_group(const limestone_schedule *,size_t group,limestone_group_info *,limestone_error *);
limestone_status limestone_schedule_group_member(const limestone_schedule *,size_t group,size_t member,uint32_t *id,limestone_error *);

limestone_allocation_document *limestone_regtl_load(const char *source,const char *source_name,limestone_error *);
void limestone_allocation_document_destroy(limestone_allocation_document *);
size_t limestone_allocation_unit_count(const limestone_allocation_document *);
const char *limestone_allocation_unit_name(const limestone_allocation_document *,size_t unit);
size_t limestone_allocation_function_count(const limestone_allocation_document *,size_t unit);
const char *limestone_allocation_function_name(const limestone_allocation_document *,size_t unit,size_t function);
const char *limestone_allocation_document_text(const limestone_allocation_document *);
/** Select the unit's explicit live-range problem instead of a function. */
#define LIMESTONE_ALLOCATION_RANGES SIZE_MAX
/** Analyze CFG liveness when selecting a function, then allocate and verify.
 * A spill result is an allocation decision; target-specific transfers are
 * materialized by the compiler pipeline, not this standalone allocation adapter.
 */
limestone_assignment *limestone_assignment_run(const limestone_allocation_document *,size_t unit,size_t function,
  limestone_allocator,limestone_error *);
/** Use document PBQP policy when options is NULL; otherwise copy/use the supplied
 * policy for this run. Returned assignment owns its result independently. */
limestone_assignment *limestone_assignment_run_pbqp(const limestone_allocation_document *,size_t unit,size_t function,
  const limestone_pbqp_options *,limestone_error *);
limestone_status limestone_assignment_cost(const limestone_assignment *,double *cost,limestone_error *);
void limestone_assignment_destroy(limestone_assignment *);
size_t limestone_assignment_count(const limestone_assignment *);
limestone_status limestone_assignment_at(const limestone_assignment *,size_t index,uint32_t *value,uint32_t *physical,limestone_error *);
limestone_status limestone_assignment_register(const limestone_assignment *,uint32_t value,uint32_t *physical,limestone_error *);
size_t limestone_assignment_spill_count(const limestone_assignment *);
limestone_status limestone_assignment_spill(const limestone_assignment *,size_t index,uint32_t *value,limestone_error *);
const char *limestone_assignment_text(const limestone_assignment *);

#ifdef __cplusplus
}
#endif
#endif
