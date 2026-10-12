#ifndef EXL_EXOLAYER_H
#define EXL_EXOLAYER_H
#include <stddef.h>
#include <stdint.h>
#ifndef EXL_DEF
#define EXL_DEF
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct exl_context exl_context_t;
typedef struct exl_library exl_library_t;
typedef enum exl_kind
#ifdef __cplusplus
  : int
#endif
{ EXL_VOID, EXL_I64, EXL_F64, EXL_PTR } exl_kind_t;
typedef struct exl_value { exl_kind_t kind; union { long long i64; double f64; void *ptr; } as; } exl_value_t;
/** @brief Exact host callback signature; argument kinds are copied on registration.
 * @var return_kind Expected result kind; EXL_VOID is permitted.
 * @var argument_kinds Borrowed input array; EXL_VOID arguments are invalid.
 * @var argument_count Number of input kinds, bounded to 65536.
 */
typedef struct exl_signature {
  exl_kind_t return_kind;
  const exl_kind_t *argument_kinds;
  size_t argument_count;
} exl_signature_t;
/** @brief Synchronous host callback. Arguments and result are borrowed for the call.
 * Userdata remains owned by the registrant. Pointer values are borrowed opaque
 * host pointers; no target ABI conversion or arbitrary native invocation occurs.
 */
typedef void (*exl_native_fn)(const exl_value_t *args, size_t nargs, exl_value_t *result, void *userdata);
/** @brief Erased native function address; its actual signature must match registration. */
typedef void (*exl_native_address_t)(void);
/** @brief Native invocation conventions; unsupported host/ABI combinations fail explicitly. */
typedef enum exl_callconv
#ifdef __cplusplus
  : int
#endif
{
  EXL_CC_HOST,     /**< The compilation host's default C ABI. */
  EXL_CC_SYSV64,   /**< System V x86-64. */
  EXL_CC_WIN64,    /**< Windows x86-64. */
  EXL_CC_CDECL,    /**< x86 C declaration convention. */
  EXL_CC_STDCALL,  /**< x86 standard call convention. */
  EXL_CC_FASTCALL, /**< x86 fast call convention. */
  EXL_CC_AAPCS64,  /**< AArch64 procedure-call standard. */
  EXL_CC_AAPCS    /**< ARM procedure-call standard. */
} exl_callconv_t;
/** @brief Maximum native arguments, matching the prepared FFItk signature contract. */
#define EXL_NATIVE_MAX_ARGS 32u
/** @brief Extension descriptor ABI version. */
#define EXL_EXTENSION_ABI_VERSION 1u
/** @brief Export this immutable descriptor as `exl_extension_descriptor`.
 * Descriptor strings, symbols, and userdata are borrowed until library close.
 * Callbacks obey exl_native_fn's host-only ABI. No initialization is invoked.
 */
typedef struct exl_extension_symbol {
  const char *name;
  exl_native_fn function;
  void *userdata;
} exl_extension_symbol_t;
typedef struct exl_extension_descriptor {
  uint32_t abi_version;
  size_t symbol_count;
  const exl_extension_symbol_t *symbols;
} exl_extension_descriptor_t;
/** @brief Create a context; NULL on allocation failure. */
EXL_DEF exl_context_t *exl_context_create(void);
/** @brief Destroy the context and its libraries. NULL is allowed.
 * @note Synchronize shared-context access. Do not destroy from a callback.
 */
EXL_DEF void exl_context_destroy(exl_context_t*);
/** @brief Register a callback; returns 0, -1 invalid, -2 conflict, -3 failure.
 * @note Names are copied. The caller keeps userdata alive until context teardown.
 */
EXL_DEF int exl_register(exl_context_t*, const char*, exl_native_fn, void*);
/** @brief Register a callback with checked argument/result types.
 * @param context Context whose registry owns a copy of the signature.
 * @param name Copied symbol name.
 * @param function Synchronous host callback.
 * @param userdata Borrowed until context teardown.
 * @param signature Borrowed during registration; no implicit conversions.
 * @return 0 on success, -1 invalid, -2 conflict, -3 failure.
 * @note Shares the host-only calling convention and reentrancy of exl_register.
 */
EXL_DEF int exl_register_typed(exl_context_t *context,const char *name,
  exl_native_fn function,void *userdata,const exl_signature_t *signature);
/** @brief Whether the optional native invocation backend is present (0 or 1).
 * @return 1 when the libffi adapter was enabled and linked, otherwise 0.
 */
EXL_DEF int exl_native_available(void);
/** @brief Prepare and register a scalar native function, experimentally.
 * @param context Registry that owns the prepared call interface.
 * @param name Copied registry symbol name.
 * @param function Borrowed native function address; caller keeps its code alive.
 * @param signature Copied exact nonvariadic signature (I64, F64, PTR; VOID result).
 * @param convention Explicit C calling convention, never a target-ISA inference.
 * @return 0 success, -1 invalid, -2 conflict, -3 failure, -4 unsupported backend/ABI.
 * @note Registration allocates a prepared descriptor. Invocation copies arguments
 * and may allocate that copy; returned pointer storage is borrowed. I64 denotes
 * a signed 64-bit C scalar, F64 a C double, PTR a C void pointer. No aggregates,
 * vectors, variadic signatures, implicit promotions, or native exception ABI.
 * Calls are synchronous on the caller's thread and may reenter the registry.
 */
EXL_DEF int exl_register_native(exl_context_t *context,const char *name,
  exl_native_address_t function,const exl_signature_t *signature,exl_callconv_t convention);
/** @brief Prepare one concrete scalar call shape of a variadic C function.
 * @param context Registry owning the prepared interface.
 * @param name Copied registry name identifying this exact argument shape.
 * @param function Borrowed native address whose code outlives registration.
 * @param signature Copied kinds for all fixed and variadic arguments and result.
 * @param fixed_argument_count Number of named arguments, from 1 through total count.
 * @param convention Explicit host C ABI; stdcall/fastcall variadics are unsupported.
 * @return 0 success, -1 invalid, -2 conflict, -3 failure, -4 unsupported backend/ABI.
 * @note Ellipsis arguments must already have their C-promoted types: I64 means
 * long long, F64 double, PTR void pointer. No inference from a format string or
 * implicit conversions occurs. exl_call enforces the complete registered shape;
 * register each additional shape separately. Ownership/reentrancy match native
 * nonvariadic calls, and at most EXL_NATIVE_MAX_ARGS total arguments are allowed.
 */
EXL_DEF int exl_register_native_variadic(exl_context_t *context,const char *name,
  exl_native_address_t function,const exl_signature_t *signature,size_t fixed_argument_count,exl_callconv_t convention);
/** @brief Query a scalar's host layout through InteropTk.
 * @param kind Non-VOID scalar kind.
 * @param size Caller-owned byte-size output.
 * @param alignment Caller-owned alignment output.
 * @return 0 on success, -1 for invalid arguments.
 * @note This is the compilation host ABI; it never describes a foreign target.
 */
EXL_DEF int exl_kind_layout(exl_kind_t kind,size_t *size,size_t *alignment);
/** @brief Invoke a callback or registered native function; 0 success, -1/-2/-3 failure.
 * @note On failure result is EXL_VOID. Calls may reenter the context. C++
 * exceptions from callbacks are contained. VOID is not a valid argument kind.
 */
EXL_DEF int exl_call(exl_context_t*, const char*, const exl_value_t*, size_t, exl_value_t*);
/** @brief Borrow the last diagnostic, valid until the next context operation. */
EXL_DEF const char *exl_last_error(const exl_context_t*);
/** @brief Load through ExtensionTk, validate ABI and all exports, then register.
 * @return Context-owned library or NULL with a diagnostic; no partial registration.
 * @note A library remains loaded until close or context destruction.
 */
EXL_DEF exl_library_t *exl_library_open(exl_context_t*, const char *path);
/** @brief Open an ordinary shared library through ExtensionTk for typed binding.
 * @param context Context that owns the library until close or destruction.
 * @param path Borrowed during loading; an explicit host library path/name.
 * @return Context-owned library or NULL with a diagnostic.
 * @note No extension descriptor is required or consumed. Native constructors
 * follow platform loader semantics; symbols are callable only after binding.
 */
EXL_DEF exl_library_t *exl_library_open_native(exl_context_t *context,const char *path);
/** @brief Resolve and bind a native symbol with an explicit host signature.
 * @param context Owning registry and library context.
 * @param library Context-owned live library; foreign handles are rejected.
 * @param name Copied registry name (may differ from symbol).
 * @param symbol Borrowed loader symbol name.
 * @param signature Exact copied scalar signature.
 * @param convention Explicit calling convention.
 * @return 0 success, -1 invalid, -2 conflict/missing, -3 failure, -4 unsupported.
 * @note The binding keeps its loader alive. Closing an active native call's
 * library is rejected, including reentrant close attempts from native code.
 */
EXL_DEF int exl_library_bind(exl_context_t *context,exl_library_t *library,
  const char *name,const char *symbol,const exl_signature_t *signature,exl_callconv_t convention);
/** @brief Bind one concrete variadic call shape from a context-owned library.
 * @param context Owning registry and library context.
 * @param library Live context-owned library; foreign handles are rejected.
 * @param name Copied registry name for the exact call shape.
 * @param symbol Borrowed loader symbol name.
 * @param signature Copied kinds of all arguments and the result.
 * @param fixed_argument_count Number of named arguments, from 1 through total count.
 * @param convention Explicit host C ABI.
 * @return 0 success, -1 invalid, -2 conflict/missing, -3 failure, -4 unsupported.
 * @note Uses exl_register_native_variadic's promoted scalar contract. The binding
 * retains its loader and prevents library close during active native calls.
 */
EXL_DEF int exl_library_bind_variadic(exl_context_t *context,exl_library_t *library,
  const char *name,const char *symbol,const exl_signature_t *signature,size_t fixed_argument_count,exl_callconv_t convention);
/** @brief Immutable owning native type. Child handles may be destroyed immediately
 * after construction; prepared registrations retain independent type ownership.
 * Layout is the host C ABI, checked by InteropTk and the native adapter. */
typedef struct exl_native_type exl_native_type_t;
/** @brief Exact fixed-width host scalar representations for data-buffer calls. */
typedef enum exl_native_kind
#ifdef __cplusplus
  : int
#endif
{
  EXL_NATIVE_VOID, /**< Void result only. */
  EXL_NATIVE_I8, EXL_NATIVE_U8, /**< int8_t, uint8_t. */
  EXL_NATIVE_I16, EXL_NATIVE_U16, /**< int16_t, uint16_t. */
  EXL_NATIVE_I32, EXL_NATIVE_U32, /**< int32_t, uint32_t. */
  EXL_NATIVE_I64, EXL_NATIVE_U64, /**< int64_t, uint64_t. */
  EXL_NATIVE_F32, EXL_NATIVE_F64, /**< float, double. */
  EXL_NATIVE_PTR /**< void pointer, including NULL. */
} exl_native_kind_t;
/** @brief Expanded descriptor/node budget, counting repeated array elements. */
#define EXL_NATIVE_MAX_TYPE_NODES 1024u
/** @brief Maximum aggregate/array nesting depth. */
#define EXL_NATIVE_MAX_TYPE_DEPTH 32u
/** @brief Maximum native type byte size, including padding. */
#define EXL_NATIVE_MAX_TYPE_BYTES 1048576u
/** @brief Create an owning scalar descriptor, experimentally.
 * @param kind Exact host scalar representation.
 * @param output Caller-owned handle output, unchanged on failure.
 * @return 0 success, -1 invalid, -3 allocation/limit failure, -4 unavailable backend/layout.
 * @note Destroy with exl_native_type_destroy. No type inference or target-ISA lookup. */
EXL_DEF int exl_native_type_scalar(exl_native_kind_t kind,exl_native_type_t **output);
/** @brief Construct a naturally aligned C struct from owning child snapshots.
 * @param fields Borrowed non-VOID field types in C declaration order.
 * @param count Nonzero field count, bounded by EXL_NATIVE_MAX_TYPE_NODES.
 * @param output Caller-owned handle output, unchanged on failure.
 * @return 0 success, -1 invalid, -3 allocation/limit failure, -4 unavailable backend/layout.
 * @note Nested structs and fixed arrays are supported. Packed/over-aligned records,
 * unions, bitfields and vector ABI types require separate adapters. */
EXL_DEF int exl_native_type_struct(const exl_native_type_t *const *fields,size_t count,exl_native_type_t **output);
/** @brief Construct a fixed C array for an aggregate field.
 * @param element Borrowed complete non-VOID type retained by the result.
 * @param count Nonzero element count within the expanded node/byte budgets.
 * @param output Caller-owned handle output, unchanged on failure.
 * @return 0 success, -1 invalid, -3 allocation/limit failure, -4 unavailable backend/layout.
 * @note Arrays cannot be top-level call arguments/results: C decays arguments
 * to pointers. Pass an explicit pointer or embed the array in a struct. */
EXL_DEF int exl_native_type_array(const exl_native_type_t *element,size_t count,exl_native_type_t **output);
/** @brief Explicit compiler/ABI-supplied layout for a custom data adapter.
 * Identity names the complete layout and ABI version. Offsets may overlap (unions)
 * or be unaligned (packed fields); bitfield/vector semantics remain in the adapter.
 * The complete representation includes padding and requires size % alignment == 0. */
typedef struct exl_custom_layout {
  const char *identity;
  size_t size,alignment;
  const size_t *field_offsets;
  size_t field_count;
} exl_custom_layout_t;
/** @brief Create an owning packed/over-aligned/union/bitfield/vector layout.
 * @param layout Borrowed explicit layout, copied on success.
 * @param output Owning handle output, unchanged on failure.
 * @return 0 success, -1 malformed, -3 allocation failure.
 * @note Custom layouts work through exl_register_data_adapter, independently of
 * libffi. Direct native registration rejects them with -4. They do not imply an
 * ABI classification and cannot be nested in automatically classified structs. */
EXL_DEF int exl_native_type_custom(const exl_custom_layout_t *layout,exl_native_type_t **output);
/** @brief Release this owning type handle. NULL is allowed; registrations and
 * parent types retain their snapshots. Synchronize shared-handle destruction.
 * @param type Handle to destroy. */
EXL_DEF void exl_native_type_destroy(exl_native_type_t *type);
/** @brief Query the immutable host byte size/alignment, including struct padding.
 * @param type Borrowed live type handle.
 * @param size Caller-owned byte-size output; VOID reports zero.
 * @param alignment Caller-owned alignment output; VOID reports zero.
 * @return 0 success, -1 invalid; outputs remain unchanged on failure. */
EXL_DEF int exl_native_type_layout(const exl_native_type_t *type,size_t *size,size_t *alignment);
/** @brief Query a struct field/array element's byte offset.
 * @param type Borrowed aggregate/array handle.
 * @param index Field/element position.
 * @param offset Caller-owned offset, unchanged on failure.
 * @return 0 success, -1 invalid kind/index/output. */
EXL_DEF int exl_native_type_offset(const exl_native_type_t *type,size_t index,size_t *offset);
/** @brief Exact native data call shape, copied on registration.
 * @var result_type Borrowed type, including an explicit VOID descriptor.
 * @var argument_types Borrowed complete non-VOID, non-array type handles.
 * @var argument_count Number of arguments, at most EXL_NATIVE_MAX_ARGS. */
typedef struct exl_data_signature {
  const exl_native_type_t *result_type;
  const exl_native_type_t *const *argument_types;
  size_t argument_count;
} exl_data_signature_t;
/** @brief Borrowed bytes containing one exact host C value representation.
 * @var data Readable buffer, copied before calling; alignment is unrestricted.
 * @var size Exact registered type size including padding. Pointer representations
 * hold borrowed host pointers; pointed-to storage is not copied or owned. */
typedef struct exl_data_argument { const void *data;size_t size; } exl_data_argument_t;
/** @brief Synchronous compiled bridge for exact custom native data shapes.
 * Arguments and result are aligned owning temporary copies, borrowed for the call.
 * The bridge uses its compiler's typed ABI and contains foreign exceptions before
 * returning 0 or -1/-2/-3/-4 with an optional bounded diagnostic. Result bytes are
 * committed only on 0. C++ bridge exceptions are contained as -3 by Exolayer. */
typedef int (*exl_data_adapter_fn)(const exl_data_argument_t *arguments,size_t count,
  void *result,size_t result_size,void *userdata,char *error,size_t error_capacity);
/** @brief Release adopted adapter userdata after the last registration/call owner. */
typedef void (*exl_data_adapter_release_fn)(void *userdata);
/** @brief Register an owning compiler/ABI-specific bridge, experimentally.
 * @param context Synchronized owning context; synchronous reentrancy is supported.
 * @param name Copied unique registry name.
 * @param function Bridge implementing the complete registered native shape.
 * @param userdata Adopted only on success; retained through active calls.
 * @param release Optional destructor invoked after the last owner is released.
 * @param signature Copied exact owning type snapshots, including custom layouts.
 * @return 0 success, -1 invalid, -2 duplicate, -3 allocation failure.
 * @note Native calling-convention/classification and exception translation belong
 * to the explicitly compiled bridge. No target ABI or promotion is inferred. */
EXL_DEF int exl_register_data_adapter(exl_context_t *context,const char *name,
  exl_data_adapter_fn function,void *userdata,exl_data_adapter_release_fn release,const exl_data_signature_t *signature);
/** @brief Remove a registration. Active calls retain code/type/userdata snapshots.
 * @return 0 success, -1 invalid, -2 missing, -3 failure. Library ownership is retained
 * by its context until close. Context destruction from an active call is forbidden. */
EXL_DEF int exl_unregister(exl_context_t *context,const char *name);
/** @brief Register a scalar/aggregate by-value function, experimentally.
 * @param context Registry owning prepared interface and type snapshots.
 * @param name Copied unique registry name.
 * @param function Borrowed exact native address; code must outlive registration.
 * @param signature Borrowed during registration, copied with type ownership.
 * @param convention Explicit supported host C calling convention.
 * @return 0 success, -1 invalid, -2 duplicate, -3 failure, -4 unsupported backend/ABI.
 * @note Uses exl_call_data, not exl_call. Foreign exceptions must not cross this
 * interface. Input/result layouts are checked independently by the native adapter. */
EXL_DEF int exl_register_native_data(exl_context_t *context,const char *name,exl_native_address_t function,
  const exl_data_signature_t *signature,exl_callconv_t convention);
/** @brief Prepare one exact variadic scalar/aggregate data call shape.
 * @param context Registry owning the prepared interface and type snapshots.
 * @param name Copied unique registry name.
 * @param function Borrowed native address; code must outlive registration.
 * @param signature Complete argument/result shape, copied with type ownership.
 * @param fixed_argument_count Named argument count, from 1 through total count.
 * @param convention Explicit host C ABI; stdcall/fastcall are unsupported.
 * @return 0 success, -1 invalid/unpromoted type, -2 duplicate, -3 failure, -4 unsupported.
 * @note Ellipsis scalars must already be C-promoted. Structs, including nested
 * records and arrays, retain their exact layout without field-wise promotions.
 * Uses exl_call_data and the same ownership and exception rules as fixed calls. */
EXL_DEF int exl_register_native_data_variadic(exl_context_t *context,const char *name,exl_native_address_t function,
  const exl_data_signature_t *signature,size_t fixed_argument_count,exl_callconv_t convention);
/** @brief Resolve a library symbol and register an owning data-call interface.
 * @param context Owning registry.
 * @param library Live context-owned loader; retained through active calls.
 * @param name Copied unique registry name.
 * @param symbol Borrowed exact native loader symbol name.
 * @param signature Copied scalar/aggregate signature and owning type snapshots.
 * @param convention Explicit supported host C calling convention.
 * @return 0 success, -1 invalid, -2 duplicate/missing, -3 failure, -4 unsupported.
 * @note Reentrant library close is rejected during an active call. */
EXL_DEF int exl_library_bind_data(exl_context_t *context,exl_library_t *library,const char *name,
  const char *symbol,const exl_data_signature_t *signature,exl_callconv_t convention);
/** @brief Bind a library symbol with an owning variadic data-call shape.
 * @param context Owning registry, synchronized by the caller.
 * @param library Live context-owned loader, retained through active calls.
 * @param name Copied unique registry name.
 * @param symbol Exact native symbol name, borrowed during binding.
 * @param signature Complete scalar/aggregate shape, copied with type ownership.
 * @param fixed_argument_count Named argument count, from 1 through total count.
 * @param convention Explicit host C calling convention.
 * @return 0 success, -1 invalid/unpromoted type, -2 duplicate/missing, -3 failure, -4 unsupported.
 * @note Follows exl_register_native_data_variadic's promotion contract and
 * exl_library_bind_data's active-call lifetime and reentrant-close rules. */
EXL_DEF int exl_library_bind_data_variadic(exl_context_t *context,exl_library_t *library,const char *name,
  const char *symbol,const exl_data_signature_t *signature,size_t fixed_argument_count,exl_callconv_t convention);
/** @brief Invoke a registered exact data-buffer native call, synchronously.
 * @param context Registry, synchronized by caller; reentrant calls are supported.
 * @param name Registered data-call name.
 * @param arguments Borrowed descriptors and bytes, copied before native execution.
 * @param count Exact registered argument count.
 * @param result Caller-owned writable exact-size buffer; NULL for a VOID result.
 * @param result_size Exact registered result size, zero for VOID.
 * @return 0 success, -1 invalid shape/buffer, -2 missing, -3 failure, -4 unsupported.
 * @note Invocation allocates aligned temporary argument/result storage. Result
 * bytes are written only on success; argument/result overlap is permitted.
 * Native pointer values remain borrowed. Native side effects are not rolled back. */
EXL_DEF int exl_call_data(exl_context_t *context,const char *name,const exl_data_argument_t *arguments,
  size_t count,void *result,size_t result_size);
/** @brief Unregister a library's callbacks and close it; returns 0 or -1/-3.
 * @note The library handle becomes invalid. Closing an active callback's library
 * is rejected. NULL/foreign library handles are rejected without dereferencing.
 */
EXL_DEF int exl_library_close(exl_context_t*, exl_library_t*);
#ifdef __cplusplus
}
#endif
#ifdef EXL_EXOLAYER_IMPLEMENTATION
#ifndef __cplusplus
#error "Exolayer's implementation translation unit requires C++20"
#endif
#include "exolayer_impl.hpp"
#endif
#endif
