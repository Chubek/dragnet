local v=require('vmweave')
return v.vm {
  name='Hooked', hooks=true, stack={cell='uint64_t',capacity=64},
  components={'insncode','dispatch','tape','compile'},
  instructions={
    {name='PUSH',operands={'cell'},stack_effect='( -- x )',semantics='vm_push(operand0);'},
    {name='HALT',flow='halt',semantics='vm_halt();'},
  },
}
