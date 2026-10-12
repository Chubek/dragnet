#ifndef LIMESTONE_H
#define LIMESTONE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct limestone_module limestone_module;
typedef struct limestone_target limestone_target;
typedef struct limestone_program limestone_program;
typedef struct limestone_configuration limestone_configuration;
typedef struct limestone_binary_architecture limestone_binary_architecture;
typedef struct limestone_binary_transform limestone_binary_transform;
typedef struct limestone_buffer limestone_buffer;
typedef struct limestone_selection_predicate limestone_selection_predicate;
typedef struct limestone_options { int optimize, schedule, allocate; } limestone_options;
typedef enum limestone_status
#ifdef __cplusplus
  : int
#endif
{
  LIMESTONE_OK=0, LIMESTONE_INVALID_ARGUMENT=1, LIMESTONE_PARSE=2,
  LIMESTONE_NOT_FOUND=3, LIMESTONE_CONFLICT=4, LIMESTONE_UNSUPPORTED=5,
  LIMESTONE_UNSATISFIABLE=6, LIMESTONE_INTERNAL=7, LIMESTONE_TIMEOUT=8,
  LIMESTONE_INTERRUPTED=9, LIMESTONE_RESOURCE_LIMIT=10
} limestone_status;
typedef struct limestone_error { limestone_status code; char message[512]; } limestone_error;
/** Owning named selection proof. JSON context owns root, bindings and covered
 * source facts, including string arguments, properties and memory contracts.
 * Views are borrowed for the callback. proved must be 0/1; failures are copied.
 * Proofs must be deterministic. Successful creation adopts userdata when release
 * is supplied; release runs once after the last attachment/active-call snapshot.
 * A callback may destroy its source predicate, target or document. */
typedef limestone_status (*limestone_selection_proof)(const char *context_json,const char *parameters_json,
  int *proved,void *userdata,limestone_error *);
typedef void (*limestone_selection_release)(void *userdata);
limestone_selection_predicate *limestone_selection_predicate_create(const char *name,
  limestone_selection_proof,void *userdata,limestone_selection_release,limestone_error *);
void limestone_selection_predicate_destroy(limestone_selection_predicate *);
typedef enum limestone_selector
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_SELECT_GLOBAL, LIMESTONE_SELECT_GREEDY, LIMESTONE_SELECT_BURS } limestone_selector;
typedef enum limestone_allocator
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_ALLOCATE_LINEAR, LIMESTONE_ALLOCATE_GREEDY, LIMESTONE_ALLOCATE_COLOR, LIMESTONE_ALLOCATE_CONSTRAINT, LIMESTONE_ALLOCATE_PBQP } limestone_allocator;
typedef struct limestone_register_cost { uint32_t physical; double cost; } limestone_register_cost;
typedef struct limestone_value_cost {
  uint32_t value;
  double spill_cost;
  const limestone_register_cost *registers;
  size_t register_count;
} limestone_value_cost;
typedef struct limestone_coalescing_cost { uint32_t first,second; double cost; } limestone_coalescing_cost;
typedef struct limestone_pbqp_options {
  size_t search_limit,cell_limit,work_limit;
  double default_spill_cost;
  const limestone_value_cost *values;
  size_t value_count;
  const limestone_coalescing_cost *coalescing;
  size_t coalescing_count;
} limestone_pbqp_options;
/** Costs must be finite/nonnegative. Defaults: spill=1, register=0, one million
 * residual search steps, eight million cells and fifty million work steps. */
void limestone_pbqp_options_default(limestone_pbqp_options *);
typedef enum limestone_dependency_kind
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_DEP_TRUE, LIMESTONE_DEP_ANTI, LIMESTONE_DEP_OUTPUT, LIMESTONE_DEP_MEMORY, LIMESTONE_DEP_CONTROL, LIMESTONE_DEP_ORDERING } limestone_dependency_kind;
typedef enum limestone_memory_ordering
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_MEMORY_RELAXED, LIMESTONE_MEMORY_ACQUIRE, LIMESTONE_MEMORY_RELEASE, LIMESTONE_MEMORY_ACQ_REL, LIMESTONE_MEMORY_SEQ_CST } limestone_memory_ordering;
typedef enum limestone_control_flow
#ifdef __cplusplus
  : int
#endif
{ LIMESTONE_FLOW_NONE, LIMESTONE_FLOW_BRANCH, LIMESTONE_FLOW_CONDITIONAL, LIMESTONE_FLOW_RETURN, LIMESTONE_FLOW_INDIRECT, LIMESTONE_FLOW_TRAP, LIMESTONE_FLOW_CALL } limestone_control_flow;
typedef struct limestone_memory_access {
  int read,write,volatile_access,atomic;
  limestone_memory_ordering ordering;
  const char *address_space;
  const uint32_t *alias_sets;
  size_t alias_count;
  uint32_t size,alignment;
} limestone_memory_access;
/** Initialize default options. Physical allocation requires a target adapter. */
void limestone_options_default(limestone_options*);
/** Compile a closed TraceML integer program. Caller owns the returned module. */
limestone_module *limestone_compile(const char *input);
/** Checked compile. Options may be NULL. Errors are copied into caller storage.
 * C++ exceptions never cross the ABI. Independent invocations are reentrant.
 */
limestone_module *limestone_compile_checked(const char *input, const limestone_options*, limestone_error*);
/** Borrow text until module destruction; NULL for a NULL module. */
const char *limestone_module_text(const limestone_module *);
/** Borrow the versioned JSON MachineIR region exchange until module destruction. */
const char *limestone_module_exchange(const limestone_module *);
/** Destroy a module. NULL is allowed. */
void limestone_module_destroy(limestone_module *);
/** Load a source-located UMD target. Caller owns the independent target handle. */
limestone_target *limestone_target_load(const char *umd, limestone_error *);
/** Load a UMD file with relative, cycle-checked includes. The complete source
 * graph is bounded to 16 MiB, 128 documents, and 32 include levels. */
limestone_target *limestone_target_load_file(const char *path,limestone_error *);
/** Load an Infobank target and its supported metadata encoding adapter. */
limestone_target *limestone_target_load_isa(const char *isa, limestone_error *);
void limestone_target_destroy(limestone_target *);
/** Snapshot a proof into every declaration with this name in all selectors.
 * NULL clears the attachment. Missing names return NOT_FOUND transactionally. */
limestone_status limestone_target_set_selection_predicate(limestone_target *,const char *name,
  const limestone_selection_predicate *,limestone_error *);
/** Create an independently owned machine-independent graph. Mutations copy inputs.
 * Handles may outlive targets; synchronize mutation and compilation of a shared handle.
 */
limestone_program *limestone_program_create(void);
void limestone_program_destroy(limestone_program *);
limestone_status limestone_program_add_node(limestone_program *, uint32_t id,
  const char *opcode, const char *type, const uint32_t *inputs, size_t input_count,
  int has_constant, int64_t constant, int required, int produces_value, limestone_error *);
limestone_status limestone_program_add_output(limestone_program *, uint32_t id, limestone_error *);
/** Set copied source constraints and effects before compilation. Boolean flags are 0/1. */
limestone_status limestone_program_set_node_properties(limestone_program *,uint32_t id,
  const char *register_class,const char *origin,int side_effect,int call,int terminator,int may_trap,limestone_error *);
limestone_status limestone_program_add_dependency(limestone_program *,uint32_t producer,uint32_t consumer,
  limestone_dependency_kind,uint32_t latency,int scheduler_only,limestone_error *);
/** Copy a node's memory effects. NULL removes them. Unknown alias sets conservatively
 * alias; alignment is 0 (unknown) or a power of two. Boolean fields must be 0/1.
 */
limestone_status limestone_program_set_memory(limestone_program *,uint32_t id,const limestone_memory_access *,limestone_error *);
/** Copy the versioned source payload {"strings":[{"index":0,"value":"..."}],
 * "properties":{...}}. NULL clears it. String indices address the original mixed
 * argument list; they must be distinct unsigned 32-bit identities. */
limestone_status limestone_program_set_metadata(limestone_program *,uint32_t id,const char *json,limestone_error *);
/** Blocks are declared in final layout order; the entry must be the first block.
 * Successors/live-outs/targets are copied and may refer to later declarations.
 */
limestone_status limestone_program_add_block(limestone_program *,uint32_t id,const char *name,
  const uint32_t *successors,size_t successor_count,const uint32_t *live_out,size_t live_out_count,limestone_error *);
limestone_status limestone_program_set_entry(limestone_program *,uint32_t block,limestone_error *);
limestone_status limestone_program_set_node_block(limestone_program *,uint32_t node,uint32_t block,limestone_error *);
limestone_status limestone_program_set_control(limestone_program *,uint32_t node,limestone_control_flow,
  const uint32_t *targets,size_t target_count,limestone_error *);
/** Compile an explicit graph. Inputs are borrowed and remain unmodified. */
limestone_module *limestone_compile_program(const limestone_program *, const limestone_target *,
  const limestone_options *, limestone_error *);
/** Compile a UMD document containing a machine and a source program. */
limestone_module *limestone_compile_umd(const char *input, const limestone_options *, limestone_error *);
/** File equivalent of limestone_compile_umd, with bounded relative includes. */
limestone_module *limestone_compile_umd_file(const char *path,const limestone_options *,limestone_error *);
size_t limestone_module_stage_count(const limestone_module *);
const char *limestone_module_stage(const limestone_module *, size_t index);
/** Independent opaque extended configuration. Destroy accepts NULL. */
limestone_configuration *limestone_configuration_create(void);
void limestone_configuration_destroy(limestone_configuration *);
limestone_status limestone_configuration_set_pipeline(limestone_configuration *,int optimize,int schedule,
  int allocate,int encode,int trace_execution,limestone_error *);
limestone_status limestone_configuration_set_algorithms(limestone_configuration *,limestone_selector,
  limestone_allocator,limestone_error *);
/** Copy PBQP costs and limits; NULL restores defaults. Value/register identities
 * are checked against the selected allocation problem during compilation. */
limestone_status limestone_configuration_set_pbqp(limestone_configuration *,const limestone_pbqp_options *,limestone_error *);
limestone_module *limestone_compile_configured(const char *,const limestone_configuration *,limestone_error *);
limestone_module *limestone_compile_program_configured(const limestone_program *,const limestone_target *,
  const limestone_configuration *,limestone_error *);
/** Closed TraceML -> explicit target selection, allocation, MachineIR and backend.
 * The target must implement constant/return or trace arithmetic/guard lowering.
 * Source/target/configuration are borrowed during the synchronous call. */
limestone_module *limestone_compile_target(const char *source,const limestone_target *,const limestone_configuration *,limestone_error *);
/** Borrow encoded bytes until module destruction. Size is 0 for absent/empty output. */
const uint8_t *limestone_module_bytes(const limestone_module *);
size_t limestone_module_byte_count(const limestone_module *);
/** Borrow selected instruction identities and opcodes in materialized order. */
size_t limestone_module_instruction_count(const limestone_module *);
const char *limestone_module_instruction_opcode(const limestone_module *,size_t index);
limestone_status limestone_module_instruction_id(const limestone_module *,size_t index,uint32_t *id,limestone_error *);
limestone_status limestone_module_value_register(const limestone_module *,uint32_t value,uint32_t *physical,limestone_error *);
/** Original spill decisions remain inspectable after materialization. */
size_t limestone_module_spill_count(const limestone_module *);
limestone_status limestone_module_spill_value(const limestone_module *,size_t index,uint32_t *value,limestone_error *);
/** Final private-frame contracts. Frame size is 0 when no frame was materialized. */
typedef struct limestone_spill_slot {
  uint32_t value;
  const char *register_class; /**< Borrowed until module destruction. */
  uint64_t offset;
  uint32_t size,alignment;
} limestone_spill_slot;
uint64_t limestone_module_frame_size(const limestone_module *);
size_t limestone_module_spill_slot_count(const limestone_module *);
limestone_status limestone_module_spill_slot(const limestone_module *,size_t index,limestone_spill_slot *,limestone_error *);
size_t limestone_module_block_count(const limestone_module *);
limestone_status limestone_module_instruction_block(const limestone_module *,size_t index,uint32_t *block,limestone_error *);
/** Infobank-based binary APIs. Architecture and output handles are independently owned. */
limestone_binary_architecture *limestone_binary_architecture_load(const char *isa,limestone_error *);
void limestone_binary_architecture_destroy(limestone_binary_architecture *);
limestone_buffer *limestone_binary_translate(const limestone_binary_architecture *source,
  const limestone_binary_architecture *target,const uint8_t *bytes,size_t count,
  uint64_t source_address,uint64_t target_address,limestone_error *);
/** Snapshot an optional owning transform from optimization.h for this call.
 * Byte input is borrowed until return. The transform and architecture handles
 * may be destroyed by a callback after their configuration has been copied. */
limestone_buffer *limestone_binary_translate_with_transform(const limestone_binary_architecture *source,
  const limestone_binary_architecture *target,const uint8_t *bytes,size_t count,
  uint64_t source_address,uint64_t target_address,const limestone_binary_transform *,limestone_error *);
limestone_buffer *limestone_binary_disassemble(const limestone_binary_architecture *,const uint8_t *,size_t,
  uint64_t address,limestone_error *);
/** Buffer storage is borrowed until destroy; text is NULL for binary buffers. */
const uint8_t *limestone_buffer_data(const limestone_buffer *);
const char *limestone_buffer_text(const limestone_buffer *);
size_t limestone_buffer_size(const limestone_buffer *);
void limestone_buffer_destroy(limestone_buffer *);
#ifdef __cplusplus
}
#endif
#endif
