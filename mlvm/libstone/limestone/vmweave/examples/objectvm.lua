local v=require("vmweave")
return v.vm {
  name="ObjectVM", stack={cell="intptr_t",capacity=64}, memory={allocator="custom"},
  components={"token","opcode","insncode","dispatch","tape","compile","memory","object","atomic","frame","module","ipc","optim","rewrite","jit"},
  instructions={
    {name="PUSH",operands={"cell"},stack_effect="( -- x )",semantics="vm_push(operand0);"},
    {name="DUP",stack_effect="( x -- x x )",semantics="cell x=vm_pop(); vm_push(x); vm_push(x);"},
    {name="DROP",stack_effect="( x -- )",semantics="(void)vm_pop();"},
    {name="CALL",operands={"label"},flow="call",semantics="(void)vm_call(operand0);"},
    {name="RETURN",flow="return",semantics="(void)vm_return();"},
    {name="HALT",flow="halt",semantics="vm_halt();"},
  },
  -- The VM author asserts this equivalence; the generator never infers it from names.
  rewrites={{from={"DUP","DROP"},to={},equivalent=true}},
}
