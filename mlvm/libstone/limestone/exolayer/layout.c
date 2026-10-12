#include "native_internal.h"
#define ITK_CTYPES_IMPLEMENTATION
#define ITK_LAYOUT_IMPLEMENTATION
#include <InteropTk/itk_layout.h>
#include <stdlib.h>

EXL_DEF int exl_kind_layout(exl_kind_t kind,size_t *size,size_t *alignment) {
  itk_type type;
  if(!size||!alignment)return -1;
  switch(kind) {
    case EXL_I64: type=itk_type_prim(ITK_KIND_LLONG);break;
    case EXL_F64: type=itk_type_prim(ITK_KIND_DOUBLE);break;
    case EXL_PTR: type=itk_type_prim(ITK_KIND_PTR);break;
    default:return -1;
  }
  *size=itk_type_size(&type);*alignment=itk_type_align(&type);return 0;
}
static itk_type exl_data_primitive(exl_native_kind_t kind) {
  static const itk_type_kind kinds[]={ITK_KIND_VOID,ITK_KIND_I8,ITK_KIND_U8,ITK_KIND_I16,ITK_KIND_U16,ITK_KIND_I32,ITK_KIND_U32,ITK_KIND_I64,ITK_KIND_U64,ITK_KIND_FLOAT,ITK_KIND_DOUBLE,ITK_KIND_PTR};
  return itk_type_prim(kinds[kind]);
}
int exl_data_scalar_layout(exl_native_kind_t kind,size_t *size,size_t *alignment) {
  itk_type type;if(!size||!alignment||kind<EXL_NATIVE_VOID||kind>EXL_NATIVE_PTR)return -1;
  type=exl_data_primitive(kind);*size=itk_type_size(&type);*alignment=itk_type_align(&type);return 0;
}
int exl_data_flat_layout(const exl_native_kind_t *kinds,size_t count,size_t *offsets,size_t *size,size_t *alignment) {
  itk_field *fields;itk_record_builder record;size_t k;int status=0;
  if(!kinds||!count||count>EXL_NATIVE_MAX_TYPE_NODES||!offsets||!size||!alignment)return -1;
  fields=(itk_field *)calloc(count,sizeof(*fields));if(!fields)return -3;
  if(!itk_record_builder_init(&record,ITK_RECORD_STRUCT,itk_layout_default_abi(),fields,count)){free(fields);return -4;}
  for(k=0;k<count;++k){itk_type type;if(kinds[k]<=EXL_NATIVE_VOID||kinds[k]>EXL_NATIVE_PTR){status=-1;break;}type=exl_data_primitive(kinds[k]);if(!itk_record_field(&record,NULL,&type)){status=-4;break;}}
  if(!status&&!itk_record_seal(&record))status=-4;
  if(!status){*size=itk_record_size(&record);*alignment=itk_record_align(&record);for(k=0;k<count;++k)offsets[k]=itk_field_offset(&record,k);}
  free(fields);return status;
}
