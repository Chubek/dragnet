# 28. Targets and machine encoding

[Contents](README.md) · [Next: Assembler](29-assembler.md)

The baseline assembler/static-link target is `x86_64-unknown-linux-gnu`, using
little-endian ELF64. `Target` records the triple, machine identity, pointer width,
endianness, and object format. `TargetRegistry` supports explicit registration
and lookup.

## Implemented instruction subset

The encoder/decoder in `mlvm/codegen.hpp` handles:

- `ret`, `nop`, and `syscall`;
- immediate-to-register and register-to-register 32-bit `mov`;
- immediate 32-bit `add` and `sub`;
- the supported `push`/`pop` register forms;
- near `call` and `jmp`.

The documented register set is eax/ecx/edx/ebx/esp/ebp/esi/edi. These spellings
belong to MLVM's restricted assembler interface; do not infer arbitrary x86
operand sizes or addressing modes from them. The push/pop opcodes are the
backend's specified `0x50+rd`/`0x58+rd` forms.

Disassembly prints unknown or truncated instruction bytes as `.byte` instead
of inventing a decoded instruction. Round-trip tests should compare supported
encodings and explicitly test truncated sequences.

## Separate target responsibilities

Object readers can recognize identities for which MLVM has no assembler or
disassembler. A TOML relocation contract can enable raw patching without adding
register allocation, instruction selection, or calling-convention lowering.
Likewise, registering a `Target` value is not a backend implementation.

When adding a target, define its instruction encoding and relocation arithmetic
from an authoritative contract, then test a complete artifact path. The current
Cephyr backend emits constant returns; general scheduling, register allocation,
and frame lowering remain planned work.

Reference: [machine API](../include/mlvm/codegen.hpp), [target model](../include/mlvm/object.hpp).
