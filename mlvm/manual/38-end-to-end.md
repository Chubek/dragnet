# 38. An end-to-end executable

[Contents](README.md) · [Next: Testing](39-testing.md)

This workflow requires Cephyr and native execution on Linux/x86-64. It uses
checked-in files and produces separate compilation, assembly, archive, and link
artifacts. Build from chapter 3 before starting.

## Compile and inspect the function

```sh
build/cephyr -c examples/answer.c -o build/answer.o
build/mlvm-nm build/answer.o
build/mlvm-objdump -d build/answer.o
```

The `answer` function returns 42. Its machine code loads the return value into
`%eax` and executes `ret`.

## Assemble startup and link

```sh
build/mlvm-as examples/start.s -o build/start.o
build/mlvm-ar q build/answer.o -o build/libanswer.a
build/mlvm-ld build/start.o build/libanswer.a -o build/answer
```

Startup defines `_start`, calls `answer`, moves `%eax` to `%edi`, loads Linux
syscall number 60 into `%eax`, and executes `syscall`. The unresolved reference
from `start.o` causes the linker to extract the `answer` member from the archive.
The object-before-archive order matters for static linking.

## Check execution

```sh
status=0
build/answer || status=$?
test "$status" -eq 42
```

The program's expected status is 42, not zero. The explicit status capture also
works in a shell script using `set -e`. No C library, hosted startup, or external
compiler is used by this workflow.

## Investigating a failure

If `cephyr` is absent, revisit the feature configuration. An undefined `answer`
suggests the wrong object, symbol spelling, or archive order. A missing `_start`
suggests startup was omitted. Inspect `.o` files and the archive instead of
feeding the linked ET_EXEC back to MLVM's relocatable-object readers.

For an assembler-only route, use the complete `exit42.s` from chapter 29 and link
its object directly. That demonstrates the native path without requiring the
optional frontend.

Reference: [integration workflow](../tests/integration/toolchain.py).
