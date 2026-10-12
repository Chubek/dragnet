# MachineIR implementation notes

MachineIR must remain architecture-neutral. Do not add an x86-, ARM-, GPU-,
or VM-specific field to the core IR when the property belongs in `MachineSpec`
or an extensible contract.

The Infobank `.isa` files are declarative input. `parser.d` should retain
unknown fields rather than reject otherwise valid future metadata. `builder.d`
normalizes stable fields and keeps the original declaration tree in
`MachineSpec.raw`.

Semantics are S-expressions and are parsed separately from the `.isa` grammar.
Do not evaluate semantic expressions in the loader.

The core IR should represent:
- virtual and physical registers;
- register classes;
- immediates and memory operands;
- explicit and implicit effects;
- control-flow and memory effects;
- machine instructions;
- basic blocks and CFG edges;
- machine functions/modules.

Scheduling, allocation, instruction selection, disassembly and binary
translation metadata are contracts on top of the core representation, not
properties of one target architecture.
