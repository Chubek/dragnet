# 29. Assembly programming

[Contents](README.md) · [Next: Object files](30-objects.md)

`mlvm-as` accepts a restricted AT&T-style syntax: one statement per line, `#`
comments, and named labels. A label may share a line with its instruction.
Output is a relocatable object, and `-o` is required.

This complete standalone entry point exits with status 42 on Linux/x86-64.
Save it as `build/exit42.s`:

```asm
.text
.globl _start
.type _start, @function
_start:
    mov $42, %edi
    mov $60, %eax
    syscall
.size _start, .-_start
```

```sh
build/mlvm-as build/exit42.s -o build/exit42.o
build/mlvm-objdump -d build/exit42.o
```

## Directives and symbols

Sections are `.text`, `.data`, and `.rodata`, also selectable through `.section`.
Supported directives include `.globl`/`.global`, `.weak`, `.type` with function
or object kind, `.size` with an integer or `.-SYMBOL`, `.balign`, `.byte`, `.long`,
and `.quad`. Data directives accept one value. Integer syntax is decimal or
`0x` hexadecimal with an optional minus sign.

`.quad SYMBOL` creates an absolute 64-bit relocation. `call SYMBOL` and
`jmp SYMBOL` create PC-relative relocations with addend -4. The assembler does
not resolve cross-object addresses by substituting guessed final values.

## Supported operand forms

Immediate arithmetic uses forms such as `add $1, %eax` and `sub $2, %eax`.
Consult chapter 28 for the instruction subset. Arbitrary addressing modes,
prefixes, numeric labels, aliases, symbol arithmetic, BSS declarations, and
feature switches are unsupported. A source-location diagnostic identifies
rejected syntax.

For a function called by the shipped startup code, use the symbol `answer` and
return the result in `%eax` with `ret`. Chapter 38 combines those artifacts.

Reference: [assembler contract](../docs/TOOLS.md#assembler), [startup example](../examples/start.s).
