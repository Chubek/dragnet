#include "exolayer/exolayer.h"
#include <assert.h>
#include <string.h>

static int bridge(const exl_data_argument_t *args,size_t count,void *result,size_t size,void *userdata,char *error,size_t capacity) {
  int64_t input=0;(void)userdata;(void)error;(void)capacity;
  assert(count==1&&size==sizeof(input));memcpy(&input,args[0].data,sizeof(input));input+=2;memcpy(result,&input,sizeof(input));return 0;
}
void exl_test_custom_c(void) {
  exl_context_t *context=exl_context_create();exl_native_type_t *type=0;size_t offsets[]={0};
  exl_custom_layout_t layout={"fixture:c99:i64:1",8,8,offsets,1};
  const exl_native_type_t *arguments[1];exl_data_signature_t signature;int64_t input=40,result=0;exl_data_argument_t arg={&input,sizeof(input)};
  assert(context&&exl_native_type_custom(&layout,&type)==0);arguments[0]=type;signature.result_type=type;signature.argument_types=arguments;signature.argument_count=1;
  assert(exl_register_data_adapter(context,"bridge",bridge,0,0,&signature)==0);exl_native_type_destroy(type);
  assert(exl_call_data(context,"bridge",&arg,1,&result,sizeof(result))==0&&result==42);exl_context_destroy(context);
}
