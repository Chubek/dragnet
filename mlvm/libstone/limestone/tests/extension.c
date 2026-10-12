#include "exolayer.h"
#include "native_fixture.h"
#include "data_fixture.h"
#include <stdarg.h>
static void extension_sum(const exl_value_t* args,size_t count,exl_value_t* result,void* userdata) {
  (void)userdata;if(count!=2)return;result->kind=EXL_I64;result->as.i64=args[0].as.i64+args[1].as.i64;
}
static const exl_extension_symbol_t symbols[]={{"extension_sum",extension_sum,NULL}};
const exl_extension_descriptor_t exl_extension_descriptor={EXL_EXTENSION_ABI_VERSION,1,symbols};
long long exl_fixture_native_sum(long long a,long long b){return a+b;}
double exl_fixture_native_mixed(long long a,double b,void *p){return (double)a+b+(p?1.0:0.0);}
void *exl_fixture_native_pointer(void *p){return p;}
void exl_fixture_native_store(void *p,long long value){*(long long *)p=value;}
long long exl_fixture_native_reenter(void *p){struct native_fixture_state *state=(struct native_fixture_state *)p;return state->close(state->context,state->library);}
double exl_fixture_native_variadic(long long count,...) {
  va_list args;long long k;double result=0;va_start(args,count);
  for(k=0;k<count;++k){long long integer=va_arg(args,long long);double real=va_arg(args,double);void *pointer=va_arg(args,void *);result+=(double)integer+real+(pointer?1.0:0.0);}
  va_end(args);return result;
}
struct exl_fixture_pair exl_fixture_data_pair(struct exl_fixture_pair value,int64_t delta,double factor) {
  value.count+=delta;value.weight*=factor;return value;
}
struct exl_fixture_small exl_fixture_data_small(struct exl_fixture_small value) {value.tag=-value.tag;value.count+=1;return value;}
struct exl_fixture_hfa exl_fixture_data_hfa(struct exl_fixture_hfa value,float delta) {value.x+=delta;value.y+=delta;value.z+=delta;value.w+=delta;return value;}
struct exl_fixture_nested exl_fixture_data_nested(struct exl_fixture_nested value) {value.tag+=1;value.pair.count+=2;value.pair.weight*=3;value.lanes[0]+=4;value.lanes[1]+=5;value.lanes[2]+=6;return value;}
struct exl_fixture_large exl_fixture_data_large(struct exl_fixture_large a,struct exl_fixture_large b) {size_t k;for(k=0;k<8;++k)a.values[k]+=b.values[k];a.weight+=b.weight;return a;}
struct exl_fixture_pair exl_fixture_data_variadic(float bias,int64_t count,...) {
  va_list arguments;int64_t index;struct exl_fixture_pair result={0,bias};va_start(arguments,count);
  for(index=0;index<count;++index) {
    struct exl_fixture_pair pair=va_arg(arguments,struct exl_fixture_pair);
    struct exl_fixture_hfa lanes=va_arg(arguments,struct exl_fixture_hfa);
    struct exl_fixture_large large=va_arg(arguments,struct exl_fixture_large);
    int32_t integer=va_arg(arguments,int32_t);double weight=va_arg(arguments,double);void *pointer=va_arg(arguments,void *);
    result.count+=pair.count+large.values[0]+integer+*(int32_t *)pointer;
    result.weight+=pair.weight+lanes.x+lanes.y+lanes.z+lanes.w+large.weight+weight;
  }
  va_end(arguments);return result;
}
