#include "exolayer.h"
#include "data_fixture.h"
#include "native_fixture.h"
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

#define ALIGNOF(T) offsetof(struct { char byte;T value; },value)
static exl_native_type_t *scalar(exl_native_kind_t kind) {exl_native_type_t *type=NULL;assert(exl_native_type_scalar(kind,&type)==0&&type);return type;}
static exl_native_type_t *record(const exl_native_type_t *const *fields,size_t count) {exl_native_type_t *type=NULL;assert(exl_native_type_struct(fields,count,&type)==0&&type);return type;}
static void layout(const exl_native_type_t *type,size_t expected,size_t align) {size_t size=0,alignment=0;assert(exl_native_type_layout(type,&size,&alignment)==0&&size==expected&&alignment==align);}
static void offset(const exl_native_type_t *type,size_t index,size_t expected) {size_t result=SIZE_MAX;assert(exl_native_type_offset(type,index,&result)==0&&result==expected);}
static void bind(exl_context_t *context,exl_library_t *library,const char *name,const char *symbol,const exl_native_type_t *result,const exl_native_type_t *const *args,size_t count) {
  exl_data_signature_t signature={result,args,count};assert(exl_library_bind_data(context,library,name,symbol,&signature,EXL_CC_HOST)==0);
}
static int8_t echo_i8(int8_t value){return value;}
static uint8_t echo_u8(uint8_t value){return value;}
static int16_t echo_i16(int16_t value){return value;}
static uint16_t echo_u16(uint16_t value){return value;}
static int32_t echo_i32(int32_t value){return value;}
static uint32_t echo_u32(uint32_t value){return value;}
static int64_t echo_i64(int64_t value){return value;}
static uint64_t echo_u64(uint64_t value){return value;}
static struct exl_fixture_pair pair_variadic(struct exl_fixture_pair initial,...) {
  va_list arguments;struct exl_fixture_pair value;va_start(arguments,initial);value=va_arg(arguments,struct exl_fixture_pair);va_end(arguments);
  initial.count+=value.count;initial.weight+=value.weight;return initial;
}
static struct exl_fixture_large large_variadic(int64_t count,...) {
  va_list arguments;struct exl_fixture_large result={{0},0};int64_t index;size_t field;
  va_start(arguments,count);
  for(index=0;index<count;++index){struct exl_fixture_large value=va_arg(arguments,struct exl_fixture_large);for(field=0;field<8;++field)result.values[field]+=value.values[field];result.weight+=value.weight;}
  va_end(arguments);return result;
}
#define ECHO(KIND,T,FUNCTION,MINIMUM,MAXIMUM) do { \
  exl_native_type_t *type=scalar(KIND);const exl_native_type_t *args[]={type};exl_data_signature_t signature={type,args,1}; \
  T value=(MINIMUM),result=0;exl_data_argument_t argument={&value,sizeof(value)}; \
  assert(exl_register_native_data(context,#FUNCTION,(exl_native_address_t)FUNCTION,&signature,EXL_CC_HOST)==0); \
  exl_native_type_destroy(type);assert(exl_call_data(context,#FUNCTION,&argument,1,&result,sizeof(result))==0&&result==value); \
  value=(MAXIMUM);assert(exl_call_data(context,#FUNCTION,&argument,1,&result,sizeof(result))==0&&result==value); \
} while(0)
int main(int argc,char **argv) {
  exl_context_t *context;exl_library_t *library;exl_native_type_t *i8,*u16,*i64,*f32,*f64,*pointer,*pair,*small,*hfa,*lanes,*nested,*values,*large,*void_type;
  const exl_native_type_t *fields[4],*arguments[3];size_t size=123,alignment=456;
  assert(argc==2);context=exl_context_create();assert(context);
  if(!exl_native_available()) {
    exl_native_type_t *output=NULL;assert(exl_native_type_scalar(EXL_NATIVE_I32,&output)==-4&&!output);exl_context_destroy(context);return 0;
  }
  ECHO(EXL_NATIVE_I8,int8_t,echo_i8,INT8_MIN,INT8_MAX);ECHO(EXL_NATIVE_U8,uint8_t,echo_u8,0,UINT8_MAX);
  ECHO(EXL_NATIVE_I16,int16_t,echo_i16,INT16_MIN,INT16_MAX);ECHO(EXL_NATIVE_U16,uint16_t,echo_u16,0,UINT16_MAX);
  ECHO(EXL_NATIVE_I32,int32_t,echo_i32,INT32_MIN,INT32_MAX);ECHO(EXL_NATIVE_U32,uint32_t,echo_u32,0,UINT32_MAX);
  ECHO(EXL_NATIVE_I64,int64_t,echo_i64,INT64_MIN,INT64_MAX);ECHO(EXL_NATIVE_U64,uint64_t,echo_u64,0,UINT64_MAX);
  i8=scalar(EXL_NATIVE_I8);u16=scalar(EXL_NATIVE_U16);i64=scalar(EXL_NATIVE_I64);f32=scalar(EXL_NATIVE_F32);f64=scalar(EXL_NATIVE_F64);pointer=scalar(EXL_NATIVE_PTR);void_type=scalar(EXL_NATIVE_VOID);
  fields[0]=i64;fields[1]=f64;pair=record(fields,2);layout(pair,sizeof(struct exl_fixture_pair),ALIGNOF(struct exl_fixture_pair));offset(pair,1,offsetof(struct exl_fixture_pair,weight));
  fields[0]=i8;fields[1]=u16;small=record(fields,2);layout(small,sizeof(struct exl_fixture_small),ALIGNOF(struct exl_fixture_small));offset(small,1,offsetof(struct exl_fixture_small,count));
  fields[0]=fields[1]=fields[2]=fields[3]=f32;hfa=record(fields,4);layout(hfa,sizeof(struct exl_fixture_hfa),ALIGNOF(struct exl_fixture_hfa));offset(hfa,3,offsetof(struct exl_fixture_hfa,w));
  lanes=NULL;assert(exl_native_type_array(f32,3,&lanes)==0);layout(lanes,3*sizeof(float),ALIGNOF(float));offset(lanes,2,2*sizeof(float));
  fields[0]=u16;fields[1]=pair;fields[2]=lanes;fields[3]=pointer;nested=record(fields,4);layout(nested,sizeof(struct exl_fixture_nested),ALIGNOF(struct exl_fixture_nested));
  offset(nested,1,offsetof(struct exl_fixture_nested,pair));offset(nested,2,offsetof(struct exl_fixture_nested,lanes));offset(nested,3,offsetof(struct exl_fixture_nested,pointer));
  values=NULL;assert(exl_native_type_array(i64,8,&values)==0);fields[0]=values;fields[1]=f64;large=record(fields,2);layout(large,sizeof(struct exl_fixture_large),ALIGNOF(struct exl_fixture_large));offset(large,1,offsetof(struct exl_fixture_large,weight));
  library=exl_library_open_native(context,argv[1]);assert(library);
  arguments[0]=pair;arguments[1]=i64;arguments[2]=f64;bind(context,library,"pair","exl_fixture_data_pair",pair,arguments,3);
  arguments[0]=small;bind(context,library,"small","exl_fixture_data_small",small,arguments,1);
  arguments[0]=hfa;arguments[1]=f32;bind(context,library,"hfa","exl_fixture_data_hfa",hfa,arguments,2);
  arguments[0]=nested;bind(context,library,"nested","exl_fixture_data_nested",nested,arguments,1);
  arguments[0]=large;arguments[1]=large;bind(context,library,"large","exl_fixture_data_large",large,arguments,2);
  arguments[0]=pointer;bind(context,library,"reenter","exl_fixture_native_reenter",i64,arguments,1);
  {
    exl_native_type_t *integer=scalar(EXL_NATIVE_I32);const exl_native_type_t *types[20];size_t index;
    exl_data_signature_t signature={pair,types,20};types[0]=f32;types[1]=i64;
    for(index=2;index<20;index+=6){types[index]=pair;types[index+1]=hfa;types[index+2]=large;types[index+3]=integer;types[index+4]=f64;types[index+5]=pointer;}
    assert(exl_library_bind_data_variadic(context,library,"var_data","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==0);
    assert(exl_library_bind_data_variadic(context,library,"var_data","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==-2);
    assert(exl_library_bind_data_variadic(context,library,"missing_var","missing",&signature,2,EXL_CC_HOST)==-2);
    assert(exl_library_bind_data_variadic(context,NULL,"bad_library","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==-1);
    assert(exl_library_bind_data_variadic(context,library,"bad_count","exl_fixture_data_variadic",&signature,0,EXL_CC_HOST)==-1);
    assert(exl_library_bind_data_variadic(context,library,"bad_count","exl_fixture_data_variadic",&signature,21,EXL_CC_HOST)==-1);
    assert(exl_library_bind_data_variadic(context,library,"bad_cc","exl_fixture_data_variadic",&signature,2,EXL_CC_STDCALL)==-4);
    types[4]=f32;assert(exl_library_bind_data_variadic(context,library,"unpromoted","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==-1);
    types[4]=i8;assert(exl_library_bind_data_variadic(context,library,"unpromoted","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==-1);
    types[4]=u16;assert(exl_library_bind_data_variadic(context,library,"unpromoted","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==-1);
    types[4]=large;signature.argument_count=2;types[0]=types[1]=pair;
    assert(exl_register_native_data_variadic(context,"pair_var",(exl_native_address_t)pair_variadic,&signature,1,EXL_CC_HOST)==0);
    signature.argument_count=20;types[0]=f32;types[1]=i64;
    assert(exl_library_bind_data_variadic(context,library,"unpromoted","exl_fixture_data_variadic",&signature,2,EXL_CC_HOST)==0);
    signature.result_type=large;signature.argument_count=3;types[0]=i64;types[1]=types[2]=large;
    assert(exl_register_native_data_variadic(context,"large_var",(exl_native_address_t)large_variadic,&signature,1,EXL_CC_HOST)==0);
    signature.argument_count=1;
    assert(exl_register_native_data_variadic(context,"empty_var",(exl_native_address_t)large_variadic,&signature,1,EXL_CC_HOST)==0);
    exl_native_type_destroy(integer);
  }
  {
    exl_native_type_t *output=pair;const exl_native_type_t *bad_fields[]={void_type};exl_data_signature_t invalid={lanes,arguments,1};
    assert(exl_native_type_struct(bad_fields,1,&output)==-1&&output==pair);
    assert(exl_native_type_array(void_type,2,&output)==-1&&output==pair);
    assert(exl_native_type_array(i64,EXL_NATIVE_MAX_TYPE_NODES,&output)==-3&&output==pair);
    assert(exl_native_type_scalar((exl_native_kind_t)999,&output)==-1&&output==pair);
    assert(exl_native_type_layout(NULL,&size,&alignment)==-1&&size==123&&alignment==456);
    size=123;assert(exl_native_type_offset(pair,2,&size)==-1&&size==123);
    assert(exl_register_native_data(context,"array",(exl_native_address_t)echo_i64,&invalid,EXL_CC_HOST)==-1);
    invalid.result_type=i64;invalid.argument_types=bad_fields;assert(exl_register_native_data(context,"void_argument",(exl_native_address_t)echo_i64,&invalid,EXL_CC_HOST)==-1);
  }
  /* Registrations and parent records retain all child descriptors. */
  exl_native_type_destroy(i8);exl_native_type_destroy(u16);exl_native_type_destroy(i64);exl_native_type_destroy(f32);exl_native_type_destroy(f64);exl_native_type_destroy(pointer);
  exl_native_type_destroy(pair);exl_native_type_destroy(small);exl_native_type_destroy(hfa);exl_native_type_destroy(lanes);exl_native_type_destroy(nested);exl_native_type_destroy(values);exl_native_type_destroy(large);exl_native_type_destroy(void_type);
  {
    struct exl_fixture_pair pair_value={3,2},result={-1,-1};struct exl_fixture_hfa lanes_value={1,2,3,4};struct exl_fixture_large large_value={{5},3};
    int32_t integer=-2,pointed=4;void *pointer_value=&pointed;double weight=0.5;float bias=0.5f;int64_t count=3;size_t index;
    exl_data_argument_t args[20];args[0].data=&bias;args[0].size=sizeof(bias);args[1].data=&count;args[1].size=sizeof(count);
    for(index=2;index<20;index+=6){
      args[index].data=&pair_value;args[index].size=sizeof(pair_value);args[index+1].data=&lanes_value;args[index+1].size=sizeof(lanes_value);
      args[index+2].data=&large_value;args[index+2].size=sizeof(large_value);args[index+3].data=&integer;args[index+3].size=sizeof(integer);
      args[index+4].data=&weight;args[index+4].size=sizeof(weight);args[index+5].data=&pointer_value;args[index+5].size=sizeof(pointer_value);
    }
    assert(exl_call_data(context,"var_data",args,20,&result,sizeof(result))==0&&result.count==30&&result.weight==47);
    result.count=-1;assert(exl_call_data(context,"var_data",args,19,&result,sizeof(result))==-1&&result.count==-1);
    args[2].size--;assert(exl_call_data(context,"var_data",args,20,&result,sizeof(result))==-1&&result.count==-1);args[2].size++;
    assert(exl_call_data(context,"var_data",args,20,&pair_value,sizeof(pair_value))==0&&pair_value.count==30&&pair_value.weight==47);
    args[0].data=args[1].data=&pair_value;args[0].size=args[1].size=sizeof(pair_value);
    assert(exl_call_data(context,"pair_var",args,2,&result,sizeof(result))==0&&result.count==60&&result.weight==94);
  }
  {
    struct exl_fixture_pair input={20,3.5},result={0,0};int64_t delta=22;double factor=2;exl_data_argument_t args[]={{&input,sizeof(input)},{&delta,sizeof(delta)},{&factor,sizeof(factor)}};
    assert(exl_call_data(context,"pair",args,3,&result,sizeof(result))==0&&result.count==42&&result.weight==7);
    assert(exl_call_data(context,"pair",args,3,&input,sizeof(input))==0&&input.count==42&&input.weight==7);
    args[0].size--;result.count=123;result.weight=456;assert(exl_call_data(context,"pair",args,3,&result,sizeof(result))==-1&&result.count==123&&result.weight==456);
    args[0].size++;assert(exl_call_data(context,"pair",args,2,&result,sizeof(result))==-1&&result.count==123);
    assert(exl_call_data(context,"pair",args,3,&result,sizeof(result)-1)==-1&&result.count==123);
    assert(exl_call_data(context,"absent",args,3,&result,sizeof(result))==-2&&result.count==123);
  }
  {
    struct exl_fixture_large input={{1,2,3,4,5,6,7,8},2.5},result;int64_t count=2;size_t index;
    exl_data_argument_t args[]={{&count,sizeof(count)},{&input,sizeof(input)},{&input,sizeof(input)}};
    assert(exl_call_data(context,"large_var",args,3,&result,sizeof(result))==0&&result.weight==5);
    for(index=0;index<8;++index)assert(result.values[index]==2*input.values[index]);
    count=0;assert(exl_call_data(context,"empty_var",args,1,&result,sizeof(result))==0&&result.weight==0);
    for(index=0;index<8;++index)assert(result.values[index]==0);
  }
  {
    struct exl_fixture_small input={-7,65534},result;unsigned char unaligned[sizeof(input)+1];exl_data_argument_t arg={unaligned+1,sizeof(input)};
    memcpy(unaligned+1,&input,sizeof(input));assert(exl_call_data(context,"small",&arg,1,&result,sizeof(result))==0&&result.tag==7&&result.count==65535);
  }
  {
    struct exl_fixture_hfa input={1,2,3,4},result;float delta=0.5f;exl_data_argument_t args[]={{&input,sizeof(input)},{&delta,sizeof(delta)}};
    assert(exl_call_data(context,"hfa",args,2,&result,sizeof(result))==0&&result.x==1.5f&&result.y==2.5f&&result.z==3.5f&&result.w==4.5f);
  }
  {
    struct exl_fixture_nested input={7,{20,2.5},{1,2,3},context},result;exl_data_argument_t arg={&input,sizeof(input)};
    assert(exl_call_data(context,"nested",&arg,1,&result,sizeof(result))==0&&result.tag==8&&result.pair.count==22&&result.pair.weight==7.5&&result.lanes[0]==5&&result.lanes[1]==7&&result.lanes[2]==9&&result.pointer==context);
  }
  {
    struct exl_fixture_large a={{1,2,3,4,5,6,7,8},2.5},b={{8,7,6,5,4,3,2,1},3.5},result;size_t k;exl_data_argument_t args[]={{&a,sizeof(a)},{&b,sizeof(b)}};
    assert(exl_call_data(context,"large",args,2,&result,sizeof(result))==0&&result.weight==6);for(k=0;k<8;++k)assert(result.values[k]==9);
  }
  {
    struct native_fixture_state state={context,library,exl_library_close};void *pointer_value=&state;int64_t result=0;exl_data_argument_t arg={&pointer_value,sizeof(pointer_value)};
    assert(exl_call_data(context,"reenter",&arg,1,&result,sizeof(result))==0&&result==-3);
  }
  assert(exl_library_close(context,library)==0);assert(exl_call_data(context,"pair",NULL,0,NULL,0)==-2);
  exl_context_destroy(context);return 0;
}
