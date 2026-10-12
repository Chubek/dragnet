local v = require("vmweave")
return v.vm {
  name = "Calculator",
  stack = { cell = "int64_t", capacity = 64 },
  instructions = {
    { name="PUSH", opcode=1, operands={"cell"}, stack_effect="( -- n )", semantics="vm_push(operand0);" },
    { name="POP", opcode=2, stack_effect="( n -- )", semantics="(void)vm_pop();" },
    { name="ADD", opcode=3, stack_effect="( a b -- sum )", semantics="cell b=vm_pop(); cell a=vm_pop(); vm_push(a+b);" },
    { name="SUB", opcode=4, stack_effect="( a b -- difference )", semantics="cell b=vm_pop(); cell a=vm_pop(); vm_push(a-b);" },
    { name="MUL", opcode=5, stack_effect="( a b -- product )", semantics="cell b=vm_pop(); cell a=vm_pop(); vm_push(a*b);" },
    { name="DIV", opcode=6, stack_effect="( a b -- quotient )", semantics=[[cell b=vm_pop(); cell a=vm_pop();
      if(!b || (a==INT64_MIN && b==-1)) { vm_fail(-10); return; } vm_push(a/b);]] },
    { name="HALT", opcode=7, flow="halt", semantics="vm_halt();" },
    { name="JUMP", opcode=8, operands={"label"}, flow="jump", semantics="vm_jump(operand0);" },
    { name="JZ", opcode=9, operands={"label"}, stack_effect="( flag -- )", flow="branch", semantics="if(!vm_pop()) vm_jump(operand0);" },
    { name="DUP", opcode=10, stack_effect="( a -- a a )", semantics="cell a=vm_pop(); vm_push(a); vm_push(a);" },
  },
}
