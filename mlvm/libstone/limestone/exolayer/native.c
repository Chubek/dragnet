#include "native_internal.h"
#include <InteropTk/itk_platform.h>

#ifdef LIMESTONE_HAS_LIBFFI
#include <ffi.h>
#include <stdlib.h>
#include <string.h>

struct exl_ffi_handle {
  ffi_cif cif;
  ffi_type *arguments[EXL_NATIVE_MAX_ARGS];
  exl_kind_t result;
  size_t count;
};
static ffi_type *exl_ffi_type(exl_kind_t kind) {
  switch(kind) {
    case EXL_VOID:return &ffi_type_void;
    case EXL_I64:return &ffi_type_sint64;
    case EXL_F64:return &ffi_type_double;
    case EXL_PTR:return &ffi_type_pointer;
    default:return NULL;
  }
}
/* ExolangTk supplies host detection and convention identity; libffi supplies
 * all actual classification, register/stack placement, and invocation logic. */
static int exl_ffi_abi(exl_callconv_t convention,ffi_abi *abi) {
  if(convention==EXL_CC_HOST){*abi=FFI_DEFAULT_ABI;return 0;}
#if defined(ITK_ARCH_X86_64) && !defined(ITK_OS_WINDOWS)
  if(convention==EXL_CC_SYSV64){*abi=FFI_UNIX64;return 0;}
#endif
#if defined(ITK_ARCH_X86_64) && defined(ITK_OS_WINDOWS)
  if(convention==EXL_CC_WIN64){*abi=FFI_DEFAULT_ABI;return 0;}
#endif
#if defined(ITK_ARCH_X86)
  if(convention==EXL_CC_CDECL){*abi=FFI_DEFAULT_ABI;return 0;}
  if(convention==EXL_CC_STDCALL){*abi=FFI_STDCALL;return 0;}
  if(convention==EXL_CC_FASTCALL){*abi=FFI_FASTCALL;return 0;}
#endif
#if defined(ITK_ARCH_AARCH64) && !defined(ITK_OS_WINDOWS) && !defined(ITK_OS_MACOS)
  if(convention==EXL_CC_AAPCS64){*abi=FFI_DEFAULT_ABI;return 0;}
#endif
#if defined(ITK_ARCH_ARM32) && !defined(ITK_OS_WINDOWS)
  if(convention==EXL_CC_AAPCS){*abi=FFI_SYSV;return 0;}
#endif
  return -4;
}
EXL_DEF int exl_native_available(void){return 1;}
int exl_ffi_prepare(const exl_signature_t *signature,exl_callconv_t convention,int variadic,size_t fixed_count,exl_ffi_handle **output) {
  exl_ffi_handle *handle;ffi_abi abi;size_t k,size,alignment;ffi_type *result;
  *output=NULL;
  if(!signature||signature->argument_count>EXL_NATIVE_MAX_ARGS||(signature->argument_count&&!signature->argument_kinds))return -1;
  if(variadic&&(!fixed_count||fixed_count>signature->argument_count))return -1;
  if(variadic&&(convention==EXL_CC_STDCALL||convention==EXL_CC_FASTCALL))return -4;
  if(exl_ffi_abi(convention,&abi))return -4;
  result=exl_ffi_type(signature->return_kind);if(!result)return -1;
  handle=(exl_ffi_handle *)calloc(1,sizeof(*handle));if(!handle)return -3;
  handle->count=signature->argument_count;handle->result=signature->return_kind;
  for(k=0;k<handle->count;++k){handle->arguments[k]=exl_ffi_type(signature->argument_kinds[k]);if(!handle->arguments[k]||signature->argument_kinds[k]==EXL_VOID){free(handle);return -1;}}
  if((variadic?ffi_prep_cif_var(&handle->cif,abi,(unsigned)fixed_count,(unsigned)handle->count,result,handle->arguments):ffi_prep_cif(&handle->cif,abi,(unsigned)handle->count,result,handle->arguments))!=FFI_OK){free(handle);return -4;}
  /* Verify the value container's layout against the authoritative type model. */
  for(k=0;k<=handle->count;++k){exl_kind_t kind=k==handle->count?handle->result:signature->argument_kinds[k];ffi_type *type=k==handle->count?result:handle->arguments[k];if(kind==EXL_VOID)continue;if(exl_kind_layout(kind,&size,&alignment)||size!=type->size||alignment!=type->alignment){free(handle);return -4;}}
  *output=handle;return 0;
}
void exl_ffi_destroy(exl_ffi_handle *handle){free(handle);}
int exl_ffi_invoke(exl_ffi_handle *handle,exl_native_address_t function,const exl_value_t *args,exl_value_t *result) {
  void *arguments[EXL_NATIVE_MAX_ARGS];size_t k;
  union {int64_t integer;double real;void *pointer;ffi_arg word;} returned;
  if(!handle||!function||!result||(handle->count&&!args))return -1;
  for(k=0;k<handle->count;++k)switch(args[k].kind){case EXL_I64:arguments[k]=(void *)&args[k].as.i64;break;case EXL_F64:arguments[k]=(void *)&args[k].as.f64;break;case EXL_PTR:arguments[k]=(void *)&args[k].as.ptr;break;default:return -1;}
  ffi_call(&handle->cif,function,&returned,arguments);result->kind=handle->result;
  switch(handle->result){case EXL_I64:result->as.i64=returned.integer;break;case EXL_F64:result->as.f64=returned.real;break;case EXL_PTR:result->as.ptr=returned.pointer;break;case EXL_VOID:result->as.i64=0;break;default:return -1;}return 0;
}
struct exl_ffi_data_type {
  ffi_type record,*type;
  ffi_type **elements;
  size_t *offsets,count;
  int array,scalar;
  exl_native_kind_t kind;
};
struct exl_ffi_data_handle {
  ffi_cif cif;
  ffi_type *arguments[EXL_NATIVE_MAX_ARGS];
  exl_ffi_data_type *result,*types[EXL_NATIVE_MAX_ARGS];
  size_t count;
};
static ffi_type *exl_ffi_data_primitive(exl_native_kind_t kind) {
  switch(kind){case EXL_NATIVE_VOID:return &ffi_type_void;case EXL_NATIVE_I8:return &ffi_type_sint8;case EXL_NATIVE_U8:return &ffi_type_uint8;case EXL_NATIVE_I16:return &ffi_type_sint16;case EXL_NATIVE_U16:return &ffi_type_uint16;case EXL_NATIVE_I32:return &ffi_type_sint32;case EXL_NATIVE_U32:return &ffi_type_uint32;case EXL_NATIVE_I64:return &ffi_type_sint64;case EXL_NATIVE_U64:return &ffi_type_uint64;case EXL_NATIVE_F32:return &ffi_type_float;case EXL_NATIVE_F64:return &ffi_type_double;case EXL_NATIVE_PTR:return &ffi_type_pointer;default:return NULL;}
}
void exl_ffi_data_type_destroy(exl_ffi_data_type *type) {if(type){free(type->elements);free(type->offsets);free(type);}}
int exl_ffi_data_scalar(exl_native_kind_t kind,exl_ffi_data_type **output) {
  exl_ffi_data_type *type;ffi_type *primitive;size_t size,alignment;
  if(!output)return -1;primitive=exl_ffi_data_primitive(kind);if(!primitive)return -1;
  if(exl_data_scalar_layout(kind,&size,&alignment))return -1;
  if(kind!=EXL_NATIVE_VOID&&(size!=primitive->size||alignment!=primitive->alignment))return -4;
  type=(exl_ffi_data_type *)calloc(1,sizeof(*type));if(!type)return -3;
  type->type=primitive;type->scalar=1;type->kind=kind;*output=type;return 0;
}
int exl_ffi_data_aggregate(exl_ffi_data_type *const *fields,size_t count,int array,exl_ffi_data_type **output) {
  exl_ffi_data_type *type;size_t k,total=0;int flat=1;
  exl_native_kind_t kinds[EXL_NATIVE_MAX_TYPE_NODES];size_t offsets[EXL_NATIVE_MAX_TYPE_NODES],size,alignment;
  if(!output||!fields||!count||count>EXL_NATIVE_MAX_TYPE_NODES)return -1;
  for(k=0;k<count;++k){if(!fields[k]||!fields[k]->type||(fields[k]->scalar&&fields[k]->kind==EXL_NATIVE_VOID))return -1;if(fields[k]->type->size>EXL_NATIVE_MAX_TYPE_BYTES-total)return -3;total+=fields[k]->type->size;if(!fields[k]->scalar)flat=0;else kinds[k]=fields[k]->kind;}
  type=(exl_ffi_data_type *)calloc(1,sizeof(*type));if(!type)return -3;
  type->elements=(ffi_type **)calloc(count+1,sizeof(*type->elements));type->offsets=(size_t *)calloc(count,sizeof(*type->offsets));
  if(!type->elements||!type->offsets){exl_ffi_data_type_destroy(type);return -3;}
  type->count=count;type->array=array;type->record.type=FFI_TYPE_STRUCT;type->record.elements=type->elements;type->type=&type->record;
  for(k=0;k<count;++k)type->elements[k]=fields[k]->type;
  if(ffi_get_struct_offsets(FFI_DEFAULT_ABI,type->type,type->offsets)!=FFI_OK){exl_ffi_data_type_destroy(type);return -4;}
  if(!type->type->size||type->type->size>EXL_NATIVE_MAX_TYPE_BYTES){exl_ffi_data_type_destroy(type);return -3;}
  if(array){for(k=0;k<count;++k)if(fields[k]!=fields[0]||type->offsets[k]!=k*fields[0]->type->size){exl_ffi_data_type_destroy(type);return -4;}if(type->type->size!=count*fields[0]->type->size){exl_ffi_data_type_destroy(type);return -4;}}
  if(flat){int status=exl_data_flat_layout(kinds,count,offsets,&size,&alignment);if(status){exl_ffi_data_type_destroy(type);return status;}if(size!=type->type->size||alignment!=type->type->alignment||memcmp(offsets,type->offsets,count*sizeof(size_t))){exl_ffi_data_type_destroy(type);return -4;}}
  *output=type;return 0;
}
int exl_ffi_data_layout(const exl_ffi_data_type *type,size_t *size,size_t *alignment) {
  if(!type||!size||!alignment)return -1;if(type->scalar&&type->kind==EXL_NATIVE_VOID){*size=*alignment=0;return 0;}*size=type->type->size;*alignment=type->type->alignment;return 0;
}
int exl_ffi_data_offset(const exl_ffi_data_type *type,size_t index,size_t *offset) {if(!type||type->scalar||index>=type->count||!offset)return -1;*offset=type->offsets[index];return 0;}
int exl_ffi_data_prepare(exl_ffi_data_type *result,exl_ffi_data_type *const *arguments,size_t count,exl_callconv_t convention,int variadic,size_t fixed_count,exl_ffi_data_handle **output) {
  exl_ffi_data_handle *handle;ffi_abi abi;size_t k;
  if(!result||result->array||!output||count>EXL_NATIVE_MAX_ARGS||(count&&!arguments))return -1;
  if(variadic&&(!fixed_count||fixed_count>count))return -1;
  if(variadic&&(convention==EXL_CC_STDCALL||convention==EXL_CC_FASTCALL))return -4;
  if(exl_ffi_abi(convention,&abi))return -4;
  handle=(exl_ffi_data_handle *)calloc(1,sizeof(*handle));if(!handle)return -3;handle->count=count;handle->result=result;
  for(k=0;k<count;++k){
    if(!arguments[k]||arguments[k]->array||(arguments[k]->scalar&&arguments[k]->kind==EXL_NATIVE_VOID)){free(handle);return -1;}
    if(variadic&&k>=fixed_count&&arguments[k]->scalar&&(arguments[k]->kind==EXL_NATIVE_F32||
       (arguments[k]->kind>=EXL_NATIVE_I8&&arguments[k]->kind<=EXL_NATIVE_U64&&arguments[k]->type->size<sizeof(int)))){free(handle);return -1;}
    handle->types[k]=arguments[k];handle->arguments[k]=arguments[k]->type;
  }
  if((variadic?ffi_prep_cif_var(&handle->cif,abi,(unsigned)fixed_count,(unsigned)count,result->type,handle->arguments):ffi_prep_cif(&handle->cif,abi,(unsigned)count,result->type,handle->arguments))!=FFI_OK){free(handle);return -4;}
  *output=handle;return 0;
}
void exl_ffi_data_destroy(exl_ffi_data_handle *handle){free(handle);}
int exl_ffi_data_invoke(exl_ffi_data_handle *handle,exl_native_address_t function,const exl_data_argument_t *args,size_t count,void *result,size_t result_size) {
  void *arguments[EXL_NATIVE_MAX_ARGS]={0},*returned;size_t k,size,alignment;int status=0;
  if(!handle||!function||count!=handle->count||(count&&!args))return -1;
  exl_ffi_data_layout(handle->result,&size,&alignment);if(size!=result_size||(size&&!result)||(!size&&result))return -1;
  for(k=0;k<count;++k)if(!args[k].data||args[k].size!=handle->types[k]->type->size)return -1;
  returned=calloc(1,size>sizeof(ffi_arg)?size:sizeof(ffi_arg));if(!returned)return -3;
  for(k=0;k<count;++k){arguments[k]=malloc(args[k].size);if(!arguments[k]){status=-3;break;}memcpy(arguments[k],args[k].data,args[k].size);}
  if(!status){ffi_call(&handle->cif,function,returned,arguments);
    /* libffi widens small integral results to ffi_arg. Reconstruct the exact
     * scalar representation instead of truncating the first bytes on big endian. */
    if(handle->result->scalar&&size<sizeof(ffi_arg)&&size){ffi_arg word=0;memcpy(&word,returned,sizeof(word));switch(handle->result->kind){
#define EXL_DATA_RETURN(k,t) case k:{t value=(t)word;memcpy(result,&value,sizeof(value));break;}
      EXL_DATA_RETURN(EXL_NATIVE_I8,int8_t) EXL_DATA_RETURN(EXL_NATIVE_U8,uint8_t)
      EXL_DATA_RETURN(EXL_NATIVE_I16,int16_t) EXL_DATA_RETURN(EXL_NATIVE_U16,uint16_t)
      EXL_DATA_RETURN(EXL_NATIVE_I32,int32_t) EXL_DATA_RETURN(EXL_NATIVE_U32,uint32_t)
#undef EXL_DATA_RETURN
      default:memcpy(result,returned,size);break;
    }}else if(size)memcpy(result,returned,size);
  }
  for(k=0;k<count;++k)free(arguments[k]);free(returned);return status;
}
#else
EXL_DEF int exl_native_available(void){return 0;}
int exl_ffi_prepare(const exl_signature_t *signature,exl_callconv_t convention,int variadic,size_t fixed_count,exl_ffi_handle **output){(void)signature;(void)convention;(void)variadic;(void)fixed_count;*output=NULL;return -4;}
void exl_ffi_destroy(exl_ffi_handle *handle){(void)handle;}
int exl_ffi_invoke(exl_ffi_handle *handle,exl_native_address_t function,const exl_value_t *args,exl_value_t *result){(void)handle;(void)function;(void)args;(void)result;return -4;}
int exl_ffi_data_scalar(exl_native_kind_t kind,exl_ffi_data_type **output){(void)kind;(void)output;return -4;}
int exl_ffi_data_aggregate(exl_ffi_data_type *const *fields,size_t count,int array,exl_ffi_data_type **output){(void)fields;(void)count;(void)array;(void)output;return -4;}
void exl_ffi_data_type_destroy(exl_ffi_data_type *type){(void)type;}
int exl_ffi_data_layout(const exl_ffi_data_type *type,size_t *size,size_t *alignment){(void)type;(void)size;(void)alignment;return -4;}
int exl_ffi_data_offset(const exl_ffi_data_type *type,size_t index,size_t *offset){(void)type;(void)index;(void)offset;return -4;}
int exl_ffi_data_prepare(exl_ffi_data_type *result,exl_ffi_data_type *const *args,size_t count,exl_callconv_t convention,int variadic,size_t fixed_count,exl_ffi_data_handle **output){(void)result;(void)args;(void)count;(void)convention;(void)variadic;(void)fixed_count;(void)output;return -4;}
void exl_ffi_data_destroy(exl_ffi_data_handle *handle){(void)handle;}
int exl_ffi_data_invoke(exl_ffi_data_handle *handle,exl_native_address_t function,const exl_data_argument_t *args,size_t count,void *result,size_t result_size){(void)handle;(void)function;(void)args;(void)count;(void)result;(void)result_size;return -4;}
#endif
