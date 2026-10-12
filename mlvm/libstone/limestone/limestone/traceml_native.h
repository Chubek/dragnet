#ifndef LIMESTONE_TRACEML_NATIVE_H
#define LIMESTONE_TRACEML_NATIVE_H
#include "traceml.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct limestone_traceml_native_backend limestone_traceml_native_backend;
typedef struct limestone_traceml_native_program limestone_traceml_native_program;
typedef struct limestone_traceml_native_state limestone_traceml_native_state;
typedef void (*limestone_traceml_native_entry)(void *frame);
typedef enum limestone_traceml_native_kind {
  LIMESTONE_TRACEML_NATIVE_PROGRAM, LIMESTONE_TRACEML_NATIVE_CLOSURE,
  LIMESTONE_TRACEML_NATIVE_DEOPTIMIZATION
} limestone_traceml_native_kind;
typedef struct limestone_traceml_native_code {
  const char *machine_ir;
  const uint8_t *bytes;
  size_t byte_count;
  limestone_traceml_native_entry entry;
  void *userdata;
  void (*release)(void *);
} limestone_traceml_native_code;
/* Backend callbacks are synchronous. lower returns borrowed IR/bytes (copied on
 * success) and installed executable ownership. That ownership is adopted only
 * when lower returns LIMESTONE_OK; release also runs on later validation failure.
 * entry calls the supplied dispatch exactly once with its opaque frame unchanged.
 * verify proves ABI/frame preservation and the emitted dispatch call. It receives
 * owning copies of IR/bytes and must not retain their borrowed pointers. */
typedef limestone_status (*limestone_traceml_native_lower)(limestone_traceml_native_kind,
  limestone_traceml_native_entry dispatch,void *,limestone_traceml_native_code *,limestone_error *);
typedef limestone_status (*limestone_traceml_native_verify)(limestone_traceml_native_kind,
  limestone_traceml_native_entry dispatch,const limestone_traceml_native_code *,void *,limestone_error *);
/* Callback userdata is adopted on create success and retained through active
 * lowering even if a callback destroys its backend or source runtime handle. */
limestone_traceml_native_backend *limestone_traceml_native_backend_create(const char *identity,size_t byte_limit,
  limestone_traceml_native_lower,limestone_traceml_native_verify,void *userdata,void (*release)(void *),limestone_error *);
void limestone_traceml_native_backend_destroy(limestone_traceml_native_backend *);
limestone_traceml_native_program *limestone_traceml_compile_native(const limestone_traceml_runtime *,const limestone_traceml_native_backend *,limestone_error *);
limestone_traceml_native_program *limestone_traceml_compile_native_closure(const limestone_traceml_value *,const limestone_traceml_native_backend *,limestone_error *);
typedef enum limestone_traceml_native_location_kind {
  LIMESTONE_TRACEML_NATIVE_REGISTER, LIMESTONE_TRACEML_NATIVE_STACK, LIMESTONE_TRACEML_NATIVE_CONSTANT
} limestone_traceml_native_location_kind;
typedef struct limestone_traceml_native_location {
  limestone_traceml_native_location_kind kind;
  uint32_t reg;
  uint64_t offset;
  int64_t constant;
} limestone_traceml_native_location;
/* Condition-only recovery retains the complete owning lexical/continuation
 * snapshot. C++ native.hpp additionally supports proof-checked value remapping. */
limestone_traceml_native_program *limestone_traceml_compile_native_guard(const limestone_traceml_guard *,const limestone_traceml_native_location *,const limestone_traceml_native_backend *,limestone_error *);
limestone_traceml_result *limestone_traceml_native_invoke(const limestone_traceml_native_program *,const limestone_traceml_value *const *arguments,size_t count,const limestone_traceml_options *,limestone_error *);
limestone_traceml_result *limestone_traceml_native_deoptimize(const limestone_traceml_native_program *,const limestone_traceml_native_state *,const limestone_traceml_options *,limestone_error *);
/* Borrowed inspection pointers remain valid until native program destruction. */
const char *limestone_traceml_native_identity(const limestone_traceml_native_program *);
const char *limestone_traceml_native_machineir(const limestone_traceml_native_program *);
const uint8_t *limestone_traceml_native_bytes(const limestone_traceml_native_program *,size_t *count);
void limestone_traceml_native_program_destroy(limestone_traceml_native_program *);
limestone_traceml_native_state *limestone_traceml_native_state_create(limestone_error *);
limestone_status limestone_traceml_native_state_set_register(limestone_traceml_native_state *,uint32_t,const limestone_traceml_value *,limestone_error *);
/* Copies the safepoint stack image; byte order is explicitly "little" or "big". */
limestone_status limestone_traceml_native_state_set_stack(limestone_traceml_native_state *,const uint8_t *,size_t,const char *byte_order,limestone_error *);
void limestone_traceml_native_state_destroy(limestone_traceml_native_state *);
#ifdef __cplusplus
}
#endif
#endif
