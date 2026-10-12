#ifndef LIMESTONE_RUNTIME_H
#define LIMESTONE_RUNTIME_H
#include "limestone.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @file runtime.h
 * Owning C adapter for Bin2Bin's synchronous translation runtime. Link
 * Limestone::core. Architectures/options are copied; region handles may outlive
 * their runtime, retaining immutable bytes and executable ownership. Such
 * handles become invalid when the runtime is destroyed. Independent runtimes
 * are reentrant; synchronize shared-runtime calls and handle destruction.
 */
typedef struct limestone_binary_runtime limestone_binary_runtime;
typedef struct limestone_translated_region limestone_translated_region;
typedef struct limestone_runtime_options { size_t hot_threshold,max_regions; } limestone_runtime_options;
/** Byte views are borrowed for an installation call or until the inspected
 * region handle is destroyed. Guest and target addresses are explicit; bytecode
 * is never assumed to be native executable code. */
typedef struct limestone_runtime_region_view {
  uint64_t guest_address,target_address;
  const uint8_t *guest_bytes;
  size_t guest_size;
  const uint8_t *bytes;
  size_t byte_count;
} limestone_runtime_region_view;
/** Host execution adapter; writes result on success and a copied diagnostic on
 * failure. The runtime retains executable ownership until an active call ends,
 * including when that call invalidates code and destroys its region handle. */
typedef limestone_status (*limestone_runtime_execute)(void *userdata,int64_t *result,limestone_error *);
/** Release executable userdata exactly once after the last owning view/call.
 * release is optional for borrowed userdata; its lifetime is then host-owned.
 * Releases may invalidate code or invoke retained regions after cache mutations
 * complete. Runtime operations during its destruction return conflict.
 * Destructors must not throw or destroy a runtime during its active operation. */
typedef void (*limestone_runtime_release)(void *userdata);
typedef struct limestone_runtime_executable {
  limestone_runtime_execute execute;
  limestone_runtime_release release;
  void *userdata;
} limestone_runtime_executable;
/** Installer receives an initially zeroed output. Every returned executable
 * record is adopted, even on failure or invalidation during installation.
 * A successful record needs execute. Installer userdata is borrowed until
 * runtime destruction; executable userdata follows its own release contract.
 * Callbacks may invalidate code and invoke existing regions; recursive prepare
 * and cache reconfiguration return conflict. Destroying the active runtime is
 * unsupported. Error storage is non-NULL and initially clear in all callbacks. */
typedef limestone_status (*limestone_runtime_installer)(const limestone_runtime_region_view *,
  limestone_runtime_executable *,void *userdata,limestone_error *);
void limestone_runtime_options_default(limestone_runtime_options *);
/** NULL options use defaults; NULL installer provides translation/heat tracking
 * without execution. Both capacity and hot threshold must be positive. */
limestone_binary_runtime *limestone_runtime_create(const limestone_binary_architecture *source,
  const limestone_binary_architecture *target,const limestone_runtime_options *,
  limestone_runtime_installer,void *userdata,limestone_error *);
/** Copy an optional binary optimizer from optimization.h into the runtime.
 * Later transform/optimizer mutation or destruction does not alter its snapshot.
 * Deadlines and cancellation disable byte-cache and resident-translation reuse;
 * observations retain heat and publish independent immutable byte views. */
limestone_binary_runtime *limestone_runtime_create_with_transform(const limestone_binary_architecture *source,
  const limestone_binary_architecture *target,const limestone_runtime_options *,const limestone_binary_transform *,
  limestone_runtime_installer,void *userdata,limestone_error *);
void limestone_runtime_destroy(limestone_binary_runtime *);
/** Copy guest bytes; identical addresses/bytes share heat. Each result is an
 * independent owning handle. Publication and hot installation are synchronous. */
limestone_translated_region *limestone_runtime_prepare(limestone_binary_runtime *,const uint8_t *,size_t,
  uint64_t guest_address,uint64_t target_address,limestone_error *);
void limestone_translated_region_destroy(limestone_translated_region *);
int limestone_translated_region_is_valid(const limestone_translated_region *);
int limestone_translated_region_is_compiled(const limestone_translated_region *);
limestone_status limestone_translated_region_get_view(const limestone_translated_region *,
  limestone_runtime_region_view *,limestone_error *);
/** Result storage is written only on success. Invalid, cold and foreign-runtime
 * handles return distinct conflict, unsupported and invalid-argument errors. */
limestone_status limestone_runtime_invoke(limestone_binary_runtime *,const limestone_translated_region *,
  int64_t *result,limestone_error *);
/** Invalidate every overlapping published view, including evicted handles.
 * invalidated counts views, not unique guest regions; written only on success. */
limestone_status limestone_runtime_invalidate(limestone_binary_runtime *,uint64_t guest_address,uint64_t size,
  size_t *invalidated,limestone_error *);
size_t limestone_runtime_resident_count(const limestone_binary_runtime *);
/** Open explicit LMDB storage; no fallback if unavailable or opening fails. */
limestone_status limestone_runtime_open_cache(limestone_binary_runtime *,const char *path,size_t map_size,
  limestone_error *);
#ifdef __cplusplus
}
#endif
#endif
