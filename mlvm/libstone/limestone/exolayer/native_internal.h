#ifndef EXL_NATIVE_INTERNAL_H
#define EXL_NATIVE_INTERNAL_H
#include "exolayer.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The libffi and FFItk headers have colliding type names. Keep the invocation
 * adapter in a separate C translation unit behind an Exolayer-owned handle. */
typedef struct exl_ffi_handle exl_ffi_handle;
int exl_ffi_prepare(const exl_signature_t *,exl_callconv_t,int variadic,size_t fixed_argument_count,exl_ffi_handle **);
void exl_ffi_destroy(exl_ffi_handle *);
int exl_ffi_invoke(exl_ffi_handle *,exl_native_address_t,const exl_value_t *,exl_value_t *);
/* Opaque native type descriptors. Aggregate layout/classification stays in the
 * isolated backend because FFItk v0.1 has no nested record type representation. */
typedef struct exl_ffi_data_type exl_ffi_data_type;
typedef struct exl_ffi_data_handle exl_ffi_data_handle;
int exl_ffi_data_scalar(exl_native_kind_t,exl_ffi_data_type **);
int exl_ffi_data_aggregate(exl_ffi_data_type *const *,size_t,int array,exl_ffi_data_type **);
void exl_ffi_data_type_destroy(exl_ffi_data_type *);
int exl_ffi_data_layout(const exl_ffi_data_type *,size_t *,size_t *);
int exl_ffi_data_offset(const exl_ffi_data_type *,size_t,size_t *);
int exl_ffi_data_prepare(exl_ffi_data_type *,exl_ffi_data_type *const *,size_t,exl_callconv_t,int variadic,size_t fixed_argument_count,exl_ffi_data_handle **);
void exl_ffi_data_destroy(exl_ffi_data_handle *);
int exl_ffi_data_invoke(exl_ffi_data_handle *,exl_native_address_t,const exl_data_argument_t *,size_t,void *,size_t);
/* Independent InteropTk host-layout checks for all primitives and flat scalar
 * records. Nested records/arrays use libffi's explicit owning type adapter. */
int exl_data_scalar_layout(exl_native_kind_t,size_t *,size_t *);
int exl_data_flat_layout(const exl_native_kind_t *,size_t,size_t *,size_t *,size_t *);
#ifdef __cplusplus
}
#endif
#endif
