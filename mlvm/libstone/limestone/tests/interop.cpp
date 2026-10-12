#include "test.hpp"
#include "exolayer.h"
#include "native_fixture.h"
#include <cstring>

void throws(const exl_value_t*,size_t,exl_value_t*,void*) {throw std::runtime_error("callback failure");}
void no_op(const exl_value_t*,size_t,exl_value_t*,void*) {}
void reentrant(const exl_value_t*,size_t,exl_value_t* result,void* user) {
  auto* context=static_cast<exl_context_t*>(user);
  for(int i=0;i<128;++i)CHECK(exl_register(context,("registered_"+std::to_string(i)).c_str(),no_op,nullptr)==0);
  result->kind=EXL_I64;result->as.i64=42;
}
static long long native_integer_stack(long long a,long long b,long long c,long long d,long long e,long long f,long long g,long long h,long long i,long long j){return a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j;}
static double native_float_stack(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j){return a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j;}
int main(int argc,char** argv){return test_main([&]{
  CHECK(argc==3);
  auto* context=exl_context_create();CHECK(context);
  std::unique_ptr<exl_context_t,decltype(&exl_context_destroy)> owned(context,exl_context_destroy);
  CHECK(exl_register(context,"throws",throws,nullptr)==0);
  exl_value_t result{EXL_I64,{7}};
  CHECK(exl_call(context,"throws",nullptr,0,&result)==-3&&result.kind==EXL_VOID);
  CHECK(std::string(exl_last_error(context)).find("callback failure")!=std::string::npos);
  CHECK(exl_register(context,"noop",no_op,nullptr)==0&&std::strlen(exl_last_error(context))==0);
  CHECK(exl_register(context,"noop",no_op,nullptr)==-2);
  CHECK(exl_call(context,"absent",nullptr,0,&result)==-2&&result.kind==EXL_VOID);
  CHECK(exl_call(context,"noop",nullptr,1,&result)==-1);
  CHECK(exl_register(context,"",no_op,nullptr)==-1&&std::strlen(exl_last_error(context))>0);
  CHECK(exl_register(context,"reentrant",reentrant,context)==0);
  CHECK(exl_call(context,"reentrant",nullptr,0,&result)==0&&result.as.i64==42);
  exl_signature_t bad_return{EXL_I64,nullptr,0};CHECK(exl_register_typed(context,"wrong_result",no_op,nullptr,&bad_return)==0);
  CHECK(exl_call(context,"wrong_result",nullptr,0,&result)==-3&&result.kind==EXL_VOID);
  CHECK(std::string(exl_last_error(context)).find("result type")!=std::string::npos);
  auto* library=exl_library_open(context,argv[1]);CHECK(library);
  exl_value_t args[]={{EXL_I64,{20}},{EXL_I64,{22}}};
  CHECK(exl_call(context,"extension_sum",args,2,&result)==0&&result.as.i64==42);
  CHECK(!exl_library_open(context,argv[1])); // Duplicate export is transactional.
  CHECK(exl_call(context,"extension_sum",args,2,&result)==0&&result.as.i64==42);
  CHECK(!exl_library_open(context,argv[2]));CHECK(std::string(exl_last_error(context)).find("incompatible")!=std::string::npos);
  CHECK(!exl_library_open(context,"/nonexistent/exolayer-extension.so"));
  CHECK(exl_library_close(context,library)==0);CHECK(exl_call(context,"extension_sum",args,2,&result)==-2);
  CHECK(exl_library_close(context,nullptr)==-1);
  const exl_kind_t pair[]={EXL_I64,EXL_I64};exl_signature_t sum_signature{EXL_I64,pair,2};
  if(exl_native_available()) {
    auto* native_library=exl_library_open_native(context,argv[1]);CHECK(native_library);
    CHECK(exl_library_bind(context,native_library,"native_sum","exl_fixture_native_sum",&sum_signature,EXL_CC_HOST)==0);
    CHECK(exl_call(context,"native_sum",args,2,&result)==0&&result.as.i64==42);
    CHECK(exl_call(context,"native_sum",args,2,args)==0&&args[0].as.i64==42);args[0].as.i64=20;
    CHECK(exl_library_bind(context,native_library,"native_sum","exl_fixture_native_sum",&sum_signature,EXL_CC_HOST)==-2);
    CHECK(exl_library_bind(context,native_library,"missing_native","no_such_symbol",&sum_signature,EXL_CC_HOST)==-2);
    CHECK(exl_library_bind(context,nullptr,"foreign","exl_fixture_native_sum",&sum_signature,EXL_CC_HOST)==-1);
    CHECK(exl_call(context,"native_sum",args,1,&result)==-1&&result.kind==EXL_VOID);
    const exl_kind_t mixed[]={EXL_I64,EXL_F64,EXL_PTR};exl_signature_t mixed_signature{EXL_F64,mixed,3};
    CHECK(exl_library_bind(context,native_library,"mixed","exl_fixture_native_mixed",&mixed_signature,EXL_CC_HOST)==0);
    exl_value_t mixed_args[3]{};mixed_args[0]=args[0];mixed_args[1].kind=EXL_F64;mixed_args[1].as.f64=21.0;mixed_args[2].kind=EXL_PTR;mixed_args[2].as.ptr=context;
    CHECK(exl_call(context,"mixed",mixed_args,3,&result)==0&&result.kind==EXL_F64&&result.as.f64==42.0);
    exl_signature_t pointer_signature{EXL_PTR,mixed+2,1};CHECK(exl_library_bind(context,native_library,"pointer","exl_fixture_native_pointer",&pointer_signature,EXL_CC_HOST)==0);
    CHECK(exl_call(context,"pointer",mixed_args+2,1,&result)==0&&result.as.ptr==context);mixed_args[2].as.ptr=nullptr;CHECK(exl_call(context,"pointer",mixed_args+2,1,&result)==0&&!result.as.ptr);
    const exl_kind_t store_kinds[]={EXL_PTR,EXL_I64};exl_signature_t store_signature{EXL_VOID,store_kinds,2};long long stored=0;exl_value_t store_args[2]{};store_args[0].kind=EXL_PTR;store_args[0].as.ptr=&stored;store_args[1]=args[1];
    CHECK(exl_library_bind(context,native_library,"store","exl_fixture_native_store",&store_signature,EXL_CC_HOST)==0&&exl_call(context,"store",store_args,2,&result)==0&&result.kind==EXL_VOID&&stored==22);
    native_fixture_state state{context,native_library,exl_library_close};exl_signature_t reenter_signature{EXL_I64,mixed+2,1};exl_value_t reenter_arg{};reenter_arg.kind=EXL_PTR;reenter_arg.as.ptr=&state;
    CHECK(exl_library_bind(context,native_library,"native_reenter","exl_fixture_native_reenter",&reenter_signature,EXL_CC_HOST)==0&&exl_call(context,"native_reenter",&reenter_arg,1,&result)==0&&result.as.i64==-3);
    CHECK(exl_call(context,"native_sum",args,2,&result)==0&&result.as.i64==42);
    exl_kind_t integer_kinds[10],float_kinds[10];exl_value_t integer_args[10]{},float_args[10]{};for(size_t k=0;k<10;++k){integer_kinds[k]=integer_args[k].kind=EXL_I64;integer_args[k].as.i64=k+1;float_kinds[k]=float_args[k].kind=EXL_F64;float_args[k].as.f64=k+1;}
    exl_signature_t integer_signature{EXL_I64,integer_kinds,10},float_signature{EXL_F64,float_kinds,10};
    CHECK(exl_register_native(context,"int_stack",reinterpret_cast<exl_native_address_t>(native_integer_stack),&integer_signature,EXL_CC_HOST)==0);
    CHECK(exl_call(context,"int_stack",integer_args,10,&result)==0&&result.as.i64==385);
    CHECK(exl_register_native(context,"float_stack",reinterpret_cast<exl_native_address_t>(native_float_stack),&float_signature,EXL_CC_HOST)==0);
    CHECK(exl_call(context,"float_stack",float_args,10,&result)==0&&result.as.f64==385.0);
    // Cross the integer and floating register-file limits in a true C99 ellipsis
    // call. This exercises ABI vararg classification and stack placement together.
    exl_kind_t variadic_kinds[31];exl_value_t variadic_args[31]{};variadic_kinds[0]=variadic_args[0].kind=EXL_I64;variadic_args[0].as.i64=10;
    for(size_t k=0;k<10;++k){auto index=1+3*k;variadic_kinds[index]=variadic_args[index].kind=EXL_I64;variadic_args[index].as.i64=k+1;variadic_kinds[index+1]=variadic_args[index+1].kind=EXL_F64;variadic_args[index+1].as.f64=k+0.5;variadic_kinds[index+2]=variadic_args[index+2].kind=EXL_PTR;variadic_args[index+2].as.ptr=k%2?context:nullptr;}
    exl_signature_t variadic_signature{EXL_F64,variadic_kinds,31};
    CHECK(exl_library_bind_variadic(context,native_library,"variadic","exl_fixture_native_variadic",&variadic_signature,1,EXL_CC_HOST)==0);
    variadic_kinds[30]=EXL_F64; // Registration retains its own complete signature.
    CHECK(exl_call(context,"variadic",variadic_args,31,&result)==0&&result.kind==EXL_F64&&result.as.f64==110.0);
    CHECK(exl_call(context,"variadic",variadic_args,30,&result)==-1&&result.kind==EXL_VOID);
    variadic_args[30].kind=EXL_F64;CHECK(exl_call(context,"variadic",variadic_args,31,&result)==-1&&result.kind==EXL_VOID);variadic_args[30].kind=EXL_PTR;variadic_kinds[30]=EXL_PTR;
    CHECK(exl_library_bind_variadic(context,native_library,"variadic","exl_fixture_native_variadic",&variadic_signature,1,EXL_CC_HOST)==-2);
    CHECK(exl_library_bind_variadic(context,native_library,"bad_fixed","exl_fixture_native_variadic",&variadic_signature,0,EXL_CC_HOST)==-1);
    CHECK(exl_library_bind_variadic(context,native_library,"bad_fixed","exl_fixture_native_variadic",&variadic_signature,32,EXL_CC_HOST)==-1);
    CHECK(exl_library_bind_variadic(context,native_library,"bad_var_abi","exl_fixture_native_variadic",&variadic_signature,1,EXL_CC_STDCALL)==-4);
    CHECK(exl_library_bind_variadic(context,nullptr,"foreign_var","exl_fixture_native_variadic",&variadic_signature,1,EXL_CC_HOST)==-1);
    CHECK(exl_register_native(context,"bad_abi",reinterpret_cast<exl_native_address_t>(native_integer_stack),&integer_signature,static_cast<exl_callconv_t>(100))==-1);
    CHECK(exl_register_native(context,"foreign_abi",reinterpret_cast<exl_native_address_t>(native_integer_stack),&integer_signature,sizeof(void*)==8?EXL_CC_STDCALL:EXL_CC_SYSV64)==-4);
    CHECK(exl_library_close(context,native_library)==0&&exl_call(context,"native_sum",args,2,&result)==-2&&exl_call(context,"variadic",variadic_args,31,&result)==-2);
  }else {CHECK(exl_register_native(context,"disabled",reinterpret_cast<exl_native_address_t>(native_integer_stack),&sum_signature,EXL_CC_HOST)==-4);CHECK(exl_register_native_variadic(context,"disabled_var",reinterpret_cast<exl_native_address_t>(native_integer_stack),&sum_signature,1,EXL_CC_HOST)==-4);}
  CHECK(exl_library_open(context,argv[1])); // Context destruction owns remaining handles.
});}
