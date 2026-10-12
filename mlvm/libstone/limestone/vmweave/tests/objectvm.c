#include "ObjectVM.c"
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
typedef struct { size_t allocated, freed; } arena;
static void *allocate(void *context,size_t size) { ((arena *)context)->allocated++; return malloc(size); }
static void release(void *context,void *value) { ((arena *)context)->freed++; free(value); }
static int compile_failure(void *context,const char *stk,const ObjectVM_tape *tape,void **handle) {
  if(!strstr(stk,"stk-00 1") || !tape) return -1;
  *handle=context; return -55;
}
static int unused_execute(void *context,void *handle,ObjectVM_vm *vm) { (void)context; (void)handle; (void)vm; return -56; }
static void release_partial(void *context,void *handle) { if(context==handle) ++*(int *)context; }
int main(void) {
  ObjectVM_vm vm, reference; arena a={0,0}; ObjectVM_allocator allocator={&a,allocate,NULL,release};
  ObjectVM_instruction data[64], original[64]; ObjectVM_tape tape={data,0,64}, copy={original,0,64};
  ObjectVM_object *object; ObjectVM_cell value=0; int payload=42, read=0;
  ObjectVM_init(&vm); CHECK(!ObjectVM_memory_set(&vm,allocator));
  object=ObjectVM_object_new(&vm,1,sizeof(payload)); CHECK(object && a.allocated==1);
  CHECK(!ObjectVM_object_write(object,0,&payload,sizeof(payload)));
  CHECK(!ObjectVM_object_read(object,0,&read,sizeof(read)) && read==42);
  CHECK(ObjectVM_object_read(object,1,&read,sizeof(read))==-4);
  CHECK(ObjectVM_memory_set(&vm,allocator)==-1);
  CHECK(!ObjectVM_object_delete(&vm,object) && a.freed==1);
  CHECK(ObjectVM_object_new(&vm,2,32)); CHECK(ObjectVM_object_new(&vm,3,16));
  CHECK(!ObjectVM_object_clear(&vm) && a.allocated==a.freed);
  object=ObjectVM_object_new(&vm,4,6); CHECK(object);
  CHECK(!ObjectVM_object_write(object,0,"hello",6));
  { char text[6]; CHECK(!ObjectVM_object_read(object,0,text,6) && !strcmp(text,"hello")); }
  CHECK(!ObjectVM_object_clear(&vm) && a.allocated==a.freed);
  CHECK(!ObjectVM_ipc_send(&vm,42)); CHECK(!ObjectVM_ipc_receive(&vm,&value) && value==42);
  CHECK(ObjectVM_ipc_receive(&vm,&value)==-2);
  { size_t i; for(i=0;i<64;++i) CHECK(!ObjectVM_ipc_send(&vm,(ObjectVM_cell)i));
    CHECK(ObjectVM_ipc_send(&vm,999)==-3);
    for(i=0;i<64;++i) CHECK(!ObjectVM_ipc_receive(&vm,&value) && value==(ObjectVM_cell)i); }
  ObjectVM_optim_store(&vm,7,&payload); CHECK(ObjectVM_optim_lookup(&vm,7)==&payload);
  ObjectVM_optim_invalidate(&vm); CHECK(!ObjectVM_optim_lookup(&vm,7));
  CHECK(ObjectVM_atomic_integer(42).value.integer==42);
  CHECK(ObjectVM_atomic_bool(23).value.boolean==1);
  CHECK(ObjectVM_atomic_nil().tag==ObjectVM_nil);
  { ObjectVM_symbol symbol={"answer",&payload}; ObjectVM_module module={"sample",&symbol,1,NULL};
    CHECK(!ObjectVM_module_validate(&module)); CHECK(ObjectVM_module_find(&module,"answer")==&payload);
    CHECK(!ObjectVM_module_load(&vm,&module) && vm.vw_module==&module); }
  CHECK(!ObjectVM_compile("PUSH 42 DUP DROP HALT",&tape));
  copy.size=tape.size; memcpy(original,data,tape.size*sizeof(data[0]));
  ObjectVM_init(&vm); ObjectVM_init(&reference);
  CHECK(!ObjectVM_run(&reference,&copy,100)); CHECK(!ObjectVM_rewrite(&tape));
  CHECK(tape.size==2 && !ObjectVM_run(&vm,&tape,100));
  CHECK(vm.vw_sp==reference.vw_sp && vm.vw_stack[0]==reference.vw_stack[0]);
  ObjectVM_init(&vm); CHECK(!ObjectVM_compile("CALL &fn HALT fn: PUSH 7 RETURN",&tape));
  CHECK(!ObjectVM_run(&vm,&tape,100)); CHECK(vm.vw_stack[0]==7 && vm.vw_frame_count==0);
  CHECK(ObjectVM_rewrite(&tape)==-9);
  CHECK(!ObjectVM_compile("DUP DROP HALT",&tape)); CHECK(ObjectVM_rewrite(&tape)==-2);
  { void *handle=(void *)&payload; CHECK(ObjectVM_jit_compile(NULL,&tape,&handle)==-9 && !handle); }
  { int released=0; void *handle=NULL;
    ObjectVM_jit_backend backend={&released,compile_failure,unused_execute,release_partial};
    CHECK(ObjectVM_jit_compile(&backend,&tape,&handle)==-55 && !handle && released==1); }
  return 0;
}
