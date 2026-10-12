#define VMWEAVE_ENABLE_NATIVE
#include "ObjectVM.c"
#include <stdio.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"native C line %d: %s\n",__LINE__,#x); return 1;}} while(0)
int main(void) {
  ObjectVM_vm state;
  ObjectVM_instruction instructions[32]; ObjectVM_tape tape={instructions,0,32};
  ObjectVM_jit_backend backend; vmweave_native_context *context=NULL; void *program=NULL;
  CHECK(!ObjectVM_jit_native_bind(NULL,&context,&backend));
  CHECK(context);
  CHECK(!ObjectVM_compile("PUSH 42 DUP DROP HALT",&tape));
  CHECK(!ObjectVM_jit_compile(&backend,&tape,&program)); CHECK(program);
  instructions[0].operands[0]=99;
  ObjectVM_init(&state); CHECK(!ObjectVM_jit_execute(&backend,program,&state));
  CHECK(state.vw_sp==1 && state.vw_stack[0]==42 && !state.vw_current);
  ObjectVM_jit_release(&backend,program); program=NULL;
  CHECK(!ObjectVM_compile("CALL &fn HALT fn: PUSH 7 RETURN",&tape));
  CHECK(!ObjectVM_jit_compile(&backend,&tape,&program));
  ObjectVM_init(&state); CHECK(!ObjectVM_jit_execute(&backend,program,&state));
  CHECK(state.vw_stack[0]==7 && state.vw_frame_count==0);
  ObjectVM_jit_release(&backend,program); program=NULL;
  vmweave_native_context_destroy(context);
  {
    const char *arguments[]={"-fpack-struct=1"}; vmweave_native_options options={0};
    options.compile_arguments=arguments; options.compile_argument_count=1;
    CHECK(!ObjectVM_jit_native_bind(&options,&context,&backend));
    CHECK(ObjectVM_jit_compile(&backend,&tape,&program)==-11 && !program);
    CHECK(strstr(vmweave_native_error(context),"ABI"));
    vmweave_native_context_destroy(context);
  }
  return 0;
}
