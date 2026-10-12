#ifndef LIMESTONE_TRACEML_H
#define LIMESTONE_TRACEML_H
#include "limestone.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct limestone_traceml_runtime limestone_traceml_runtime;
typedef struct limestone_traceml_value limestone_traceml_value;
typedef struct limestone_traceml_result limestone_traceml_result;
typedef struct limestone_traceml_guard limestone_traceml_guard;
typedef struct limestone_traceml_options {
  size_t step_limit, event_limit;
  int record_trace, record_guards;
  /* Synchronous borrowed callback/userdata; never retained by snapshots. */
  int (*cancelled)(void *);
  void *userdata;
} limestone_traceml_options;
void limestone_traceml_options_default(limestone_traceml_options *);
/* Every returned handle owns its data and has a corresponding destroy function.
 * Runtime/closure/guard snapshots may outlive their source and parent results.
 * Calls are synchronous; independently owned immutable handles can be shared.
 * node_limit bounds the immutable TraceLambda snapshot. */
limestone_traceml_runtime *limestone_traceml_prepare(const char *source,size_t node_limit,limestone_error *);
void limestone_traceml_runtime_destroy(limestone_traceml_runtime *);
limestone_traceml_value *limestone_traceml_integer(int64_t,limestone_error *);
void limestone_traceml_value_destroy(limestone_traceml_value *);
limestone_status limestone_traceml_value_integer(const limestone_traceml_value *,int64_t *result,limestone_error *);
int limestone_traceml_value_callable(const limestone_traceml_value *);
/* Arguments are borrowed immutable values, applied lazily to the final form. */
limestone_traceml_result *limestone_traceml_invoke(const limestone_traceml_runtime *,const limestone_traceml_value *const *arguments,size_t count,const limestone_traceml_options *,limestone_error *);
limestone_traceml_result *limestone_traceml_apply(const limestone_traceml_value *,const limestone_traceml_value *const *arguments,size_t count,const limestone_traceml_options *,limestone_error *);
limestone_traceml_value *limestone_traceml_result_value(const limestone_traceml_result *,limestone_error *);
size_t limestone_traceml_result_guard_count(const limestone_traceml_result *);
limestone_traceml_guard *limestone_traceml_result_guard(const limestone_traceml_result *,size_t index,limestone_error *);
/* Borrowed until result destruction. Empty when tracing was disabled. */
const char *limestone_traceml_result_trace(const limestone_traceml_result *);
void limestone_traceml_result_destroy(limestone_traceml_result *);
int limestone_traceml_guard_expected(const limestone_traceml_guard *);
void limestone_traceml_guard_destroy(limestone_traceml_guard *);
/* Restore the owning lexical/continuation state at the guard safepoint. The
 * backend supplies the actual condition and owns native register/stack recovery. */
limestone_traceml_result *limestone_traceml_resume(const limestone_traceml_guard *,int64_t condition,const limestone_traceml_options *,limestone_error *);
#ifdef __cplusplus
}
#endif
#endif
