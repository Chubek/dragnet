#include "limestone/traceml.h"
#include "limestone/traceml_native.h"
#include <assert.h>
#include <string.h>
static int backend_releases=0,code_releases=0;
static limestone_traceml_native_backend *active_backend;
static limestone_traceml_runtime *active_runtime;
static void release_backend(void *unused){(void)unused;++backend_releases;}
static void release_code(void *unused){(void)unused;++code_releases;}
static limestone_status lower(limestone_traceml_native_kind kind,limestone_traceml_native_entry dispatch,void *unused,limestone_traceml_native_code *code,limestone_error *error){
  static const uint8_t bytes[]={0};(void)kind;(void)unused;(void)error;
  /* Direct dispatch is an already compiler-built runtime entry. This fixture
   * tests C ownership and ABI marshaling; emitted machine code is covered by the
   * x86-64 runtime-call backend in traceml_native.cpp. */
  code->machine_ir="module c_runtime { call.runtime frame; ret; }";code->bytes=bytes;code->byte_count=1;code->entry=dispatch;code->release=release_code;
  if(active_backend){limestone_traceml_native_backend_destroy(active_backend);active_backend=NULL;}
  if(active_runtime){limestone_traceml_runtime_destroy(active_runtime);active_runtime=NULL;}
  assert(backend_releases==(kind==LIMESTONE_TRACEML_NATIVE_DEOPTIMIZATION?1:0));return LIMESTONE_OK;
}
static limestone_status verify(limestone_traceml_native_kind kind,limestone_traceml_native_entry dispatch,const limestone_traceml_native_code *code,void *unused,limestone_error *error){(void)unused;(void)error;assert(code->entry==dispatch&&code->byte_count==1&&backend_releases==(kind==LIMESTONE_TRACEML_NATIVE_DEOPTIMIZATION?1:0));return LIMESTONE_OK;}
int main(void) {
  limestone_error error;limestone_traceml_options options;limestone_traceml_options_default(&options);options.record_guards=1;
  limestone_traceml_runtime *runtime=limestone_traceml_prepare("(lambda x (lambda y (add x (if y 2 0))))",1000,&error);assert(runtime);
  limestone_traceml_value *forty=limestone_traceml_integer(40,&error),*one=limestone_traceml_integer(1,&error);const limestone_traceml_value *args[]={forty};
  limestone_traceml_result *result=limestone_traceml_invoke(runtime,args,1,&options,&error);assert(result);
  limestone_traceml_value *function=limestone_traceml_result_value(result,&error);assert(limestone_traceml_value_callable(function));int64_t value=7;assert(limestone_traceml_value_integer(function,&value,&error)==LIMESTONE_UNSUPPORTED&&value==7);
  limestone_traceml_runtime_destroy(runtime);limestone_traceml_result_destroy(result);limestone_traceml_value_destroy(forty);
  args[0]=one;result=limestone_traceml_apply(function,args,1,&options,&error);assert(result&&limestone_traceml_result_guard_count(result)==1&&strstr(limestone_traceml_result_trace(result),"taken"));
  limestone_traceml_guard *guard=limestone_traceml_result_guard(result,0,&error);assert(guard&&limestone_traceml_guard_expected(guard)==1);limestone_traceml_value *integer=limestone_traceml_result_value(result,&error);assert(limestone_traceml_value_integer(integer,&value,&error)==LIMESTONE_OK&&value==42);
  limestone_traceml_value_destroy(integer);limestone_traceml_value_destroy(function);limestone_traceml_value_destroy(one);limestone_traceml_result_destroy(result);
  result=limestone_traceml_resume(guard,0,NULL,&error);assert(result);integer=limestone_traceml_result_value(result,&error);assert(limestone_traceml_value_integer(integer,&value,&error)==LIMESTONE_OK&&value==40);
  options.step_limit=0;assert(!limestone_traceml_resume(guard,1,&options,&error)&&error.code==LIMESTONE_RESOURCE_LIMIT);
  limestone_traceml_value_destroy(integer);limestone_traceml_result_destroy(result);limestone_traceml_guard_destroy(guard);
   assert(!limestone_traceml_prepare(NULL,1000,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
   active_runtime=limestone_traceml_prepare("(lambda x (if x 42 40))",1000,&error);assert(active_runtime);
   active_backend=limestone_traceml_native_backend_create("compiled-c-runtime:1",100,lower,verify,NULL,release_backend,&error);assert(active_backend);
   limestone_traceml_native_program *native=limestone_traceml_compile_native(active_runtime,active_backend,&error);assert(native&&backend_releases==1);
   size_t bytes=0;assert(limestone_traceml_native_bytes(native,&bytes)&&bytes==1&&strstr(limestone_traceml_native_machineir(native),"call.runtime"));
   one=limestone_traceml_integer(1,&error);args[0]=one;limestone_traceml_options_default(&options);options.record_guards=1;
   result=limestone_traceml_native_invoke(native,args,1,&options,&error);assert(result);guard=limestone_traceml_result_guard(result,0,&error);assert(guard);integer=limestone_traceml_result_value(result,&error);assert(limestone_traceml_value_integer(integer,&value,&error)==LIMESTONE_OK&&value==42);
   limestone_traceml_native_program_destroy(native);assert(code_releases==1);limestone_traceml_value_destroy(integer);limestone_traceml_result_destroy(result);
   limestone_traceml_native_backend *backend=limestone_traceml_native_backend_create("compiled-c-exit:1",100,lower,verify,NULL,NULL,&error);assert(backend);
   limestone_traceml_native_location location={LIMESTONE_TRACEML_NATIVE_STACK,0,0,0};native=limestone_traceml_compile_native_guard(guard,&location,backend,&error);assert(native);limestone_traceml_guard_destroy(guard);limestone_traceml_native_backend_destroy(backend);
   limestone_traceml_native_state *state=limestone_traceml_native_state_create(&error);uint8_t stack[8]={0};assert(state&&limestone_traceml_native_state_set_stack(state,stack,8,"little",&error)==LIMESTONE_OK);
   result=limestone_traceml_native_deoptimize(native,state,NULL,&error);assert(result);integer=limestone_traceml_result_value(result,&error);assert(limestone_traceml_value_integer(integer,&value,&error)==LIMESTONE_OK&&value==40);
   assert(!limestone_traceml_native_invoke(native,NULL,0,NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
   limestone_traceml_value_destroy(integer);limestone_traceml_result_destroy(result);limestone_traceml_native_state_destroy(state);limestone_traceml_native_program_destroy(native);limestone_traceml_value_destroy(one);assert(code_releases==2);return 0;
}
