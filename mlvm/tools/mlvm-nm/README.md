# mlvm-nm

Lists ELF32/ELF64 relocatable symbols in either byte order, including ELF archive
members, with stable name sorting. Library-backed options:

- `-g`/`--extern-only`, `-u`/`--undefined-only`, `--defined-only`.
- `-n`/`--numeric-sort`, `-p`/`--no-sort`.

Rows report value, local/global/weak binding, section/UND/ABS and name. Conflicting
filters/sorts fail. Uses `MLVM::Binutils` over Limestone and vendored klib parsing.
See [TOOLS.md](../../docs/TOOLS.md) for format limits and feature availability.
