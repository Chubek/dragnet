#ifdef SEPARATE
#include "Calculator.h"
#else
#include "Calculator.c"
#endif
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static int run(Calculator_vm *vm, const Calculator_tape *tape, size_t budget) {
#ifdef DIRECT
  Calculator_threaded code[256];
  int status=Calculator_thread(tape,code,256); if(status) return status;
  return Calculator_run_direct(vm,code,tape->size,budget);
#else
  return Calculator_run(vm,tape,budget);
#endif
}
int main(void) {
  Calculator_instruction storage[256]; Calculator_tape tape={storage,0,256}; Calculator_vm vm;
  Calculator_init(&vm);
  CHECK(!Calculator_compile("PUSH 10 PUSH 20 ADD HALT",&tape)); CHECK(!run(&vm,&tape,100));
  CHECK(vm.vw_sp==1 && vm.vw_stack[0]==30);
  Calculator_init(&vm);
  CHECK(!Calculator_compile("PUSH 10 PUSH 4 SUB PUSH 7 MUL PUSH 2 DIV DUP POP HALT",&tape));
  CHECK(!run(&vm,&tape,100)); CHECK(vm.vw_sp==1 && vm.vw_stack[0]==21);
  Calculator_init(&vm);
  CHECK(!Calculator_compile("PUSH 0 JZ &yes PUSH 999 yes: PUSH 42 JUMP &end PUSH 999 end: HALT",&tape));
  CHECK(!run(&vm,&tape,100)); CHECK(vm.vw_sp==1 && vm.vw_stack[0]==42);
  Calculator_init(&vm); CHECK(!Calculator_compile("ADD HALT",&tape)); CHECK(run(&vm,&tape,100)==-2);
  Calculator_init(&vm); CHECK(!Calculator_compile("PUSH 1 PUSH 0 DIV HALT",&tape)); CHECK(run(&vm,&tape,100)==-10);
  Calculator_init(&vm); CHECK(!Calculator_compile("again: JUMP &again",&tape)); CHECK(run(&vm,&tape,20)==-7);
  Calculator_init(&vm); CHECK(!Calculator_compile("PUSH 1 again: DUP JUMP &again",&tape)); CHECK(run(&vm,&tape,200)==-3);
  CHECK(Calculator_compile("JUMP &missing",&tape)==-4);
  CHECK(Calculator_compile("PUSH",&tape)==-4);
  CHECK(Calculator_compile("PUSH 99999999999999999999999999",&tape)==-4);
  CHECK(Calculator_compile("x: HALT x: HALT",&tape)==-4);
  CHECK(Calculator_compile("MISSING",&tape)==-4);
  CHECK(Calculator_compile("JUMP -1",&tape)==-4);
  CHECK(Calculator_compile("JUMP 999",&tape)==-4);
  Calculator_init(&vm); CHECK(Calculator_step(&vm,2147483647)==-1);
  CHECK(!Calculator_compile("HALT",&tape));
  { Calculator_instruction saved=storage[0]; size_t size=tape.size;
    CHECK(Calculator_compile("PUSH 123 MISSING",&tape)==-4);
    CHECK(size==tape.size && !memcmp(&saved,&storage[0],sizeof(saved))); }
  storage[0].operand_count=16; CHECK(Calculator_tape_validate(&tape)==-4);
  return 0;
}
