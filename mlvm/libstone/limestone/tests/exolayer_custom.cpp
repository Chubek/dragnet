#include "test.hpp"
#include "exolayer/exolayer.h"
#include <cstring>
#include <cstdint>

extern "C" void exl_test_custom_c(void);
struct alignas(32) Wide {uint64_t value[4];};
union Number {uint64_t integer;double real;};
struct Bits {unsigned low:5;unsigned high:11;};
#if defined(__GNUC__) || defined(__clang__)
struct __attribute__((packed)) Packed {uint8_t tag;uint64_t value;};
typedef int32_t Vector __attribute__((vector_size(16)));
static Packed native_packed(Packed value){value.value+=value.tag;return value;}
static Vector native_vector(Vector value){return value+Vector{1,2,3,4};}
#endif
static Wide native_wide(Wide value){for(auto& item:value.value)item+=2;return value;}
static Number native_union(Number value){value.integer+=2;return value;}
static Bits native_bits(Bits value){value.high+=value.low;return value;}
template<class T,T (*Function)(T)> static int compiled_bridge(const exl_data_argument_t* args,size_t count,void* output,size_t size,void*,char*,size_t){
  CHECK(count==1&&size==sizeof(T)&&reinterpret_cast<uintptr_t>(args[0].data)%alignof(T)==0&&reinterpret_cast<uintptr_t>(output)%alignof(T)==0);T input;std::memcpy(&input,args[0].data,sizeof(T));auto result=Function(input);std::memcpy(output,&result,sizeof(T));return 0;
}
template<class T> static exl_native_type_t* type(){exl_custom_layout_t layout{"fixture:compiler-native-shape:1",sizeof(T),alignof(T),nullptr,0};exl_native_type_t* result=nullptr;CHECK(exl_native_type_custom(&layout,&result)==0);return result;}
template<class T,T (*Function)(T)> static T call(exl_context_t* context,const char* name,T input){
  auto handle=type<T>();const exl_native_type_t* args[]{handle};exl_data_signature_t shape{handle,args,1};CHECK(exl_register_data_adapter(context,name,compiled_bridge<T,Function>,nullptr,nullptr,&shape)==0);
  CHECK(exl_register_native_data(context,"unsupported",reinterpret_cast<exl_native_address_t>(Function),&shape,EXL_CC_HOST)==-4);exl_native_type_destroy(handle);T result{};exl_data_argument_t argument{&input,sizeof(T)};CHECK(exl_call_data(context,name,&argument,1,&result,sizeof(T))==0);return result;
}
struct State {exl_context_t* context;size_t released=0;bool fail=false;};
static void release(void* user){++static_cast<State*>(user)->released;}
static int reentrant(const exl_data_argument_t* input,size_t,void* output,size_t size,void* user,char* error,size_t capacity){auto& state=*static_cast<State*>(user);CHECK(exl_unregister(state.context,"owned")==0&&state.released==0);std::memcpy(output,input[0].data,size);if(state.fail){std::strncpy(error,"foreign exception translated by bridge",capacity-1);return -3;}return 0;}
int main(){return test_main([]{
  exl_test_custom_c();auto context=exl_context_create();CHECK(context);std::unique_ptr<exl_context_t,decltype(&exl_context_destroy)> owner(context,exl_context_destroy);
  auto wide=call<Wide,native_wide>(context,"wide",{{40,1,2,3}});CHECK(wide.value[0]==42&&wide.value[3]==5);
  Number n{};n.integer=40;CHECK((call<Number,native_union>(context,"union",n).integer==42));Bits bits{2,40};CHECK((call<Bits,native_bits>(context,"bits",bits).high==42));
#if defined(__GNUC__) || defined(__clang__)
  CHECK((call<Packed,native_packed>(context,"packed",{2,40}).value==42));auto vector=call<Vector,native_vector>(context,"vector",Vector{41,40,39,38});for(unsigned k=0;k<4;++k)CHECK(vector[k]==42);
#endif
  auto handle=type<Wide>();const exl_native_type_t* arguments[]{handle};exl_data_signature_t shape{handle,arguments,1};State state{context};
  for(bool fail:{false,true}){state.fail=fail;state.released=0;CHECK(exl_register_data_adapter(context,"owned",reentrant,&state,release,&shape)==0);CHECK(exl_register_data_adapter(context,"owned",reentrant,&state,release,&shape)==-2&&state.released==0);Wide input{{40}},output{{99}};exl_data_argument_t arg{&input,sizeof(input)};auto status=exl_call_data(context,"owned",&arg,1,&output,sizeof(output));CHECK(state.released==1&&status==(fail?-3:0)&&output.value[0]==(fail?99:40));if(fail)CHECK(std::string(exl_last_error(context)).find("foreign exception")!=std::string::npos);}
  auto throws=[](const exl_data_argument_t*,size_t,void*,size_t,void*,char*,size_t)->int{throw std::runtime_error("bridge exception");};CHECK(exl_register_data_adapter(context,"throws",throws,nullptr,nullptr,&shape)==0);Wide output{{99}},input{{40}};exl_data_argument_t arg{&input,sizeof(input)};CHECK(exl_call_data(context,"throws",&arg,1,&output,sizeof(output))==-3&&output.value[0]==99);
  CHECK(exl_call_data(context,"wide",&arg,1,&output,sizeof(output)-1)==-1&&output.value[0]==99);exl_native_type_destroy(handle);
  exl_custom_layout_t invalid{"invalid",8,3,nullptr,0};exl_native_type_t* unchanged=nullptr;CHECK(exl_native_type_custom(&invalid,&unchanged)==-1&&!unchanged);invalid.alignment=8;size_t offsets[]{8};invalid.field_offsets=offsets;invalid.field_count=1;CHECK(exl_native_type_custom(&invalid,&unchanged)==-1&&!unchanged);
});}
