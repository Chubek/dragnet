# mlvm-size

Library-backed ELF32/64 and archive size reports, including NOBITS logical size.
`-A`/`--format=sysv` (default) reports content sections and total;
`-B`/`--format=berkeley` reports allocated text/read-only, data, bss and total.
`--radix=d|o|x` selects numeric output. Uses the shared Limestone object adapter.

See [TOOLS.md](../../docs/TOOLS.md) for resource limits and fallback behavior.
