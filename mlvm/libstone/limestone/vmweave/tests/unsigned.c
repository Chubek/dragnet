#include "Calculator.c"
int main(void) {
  Calculator_instruction code[16]; Calculator_tape tape={code,0,16}; Calculator_vm vm;
  Calculator_init(&vm);
  if(Calculator_compile("PUSH 18446744073709551615 PUSH 1 ADD HALT",&tape)) return 1;
  if(Calculator_run(&vm,&tape,100) || vm.vw_sp!=1 || vm.vw_stack[0]!=0) return 2;
  if(Calculator_compile("PUSH -1",&tape)!=-4) return 3;
  if(Calculator_compile("JUMP 18446744073709551615",&tape)!=-4) return 4;
  return 0;
}
