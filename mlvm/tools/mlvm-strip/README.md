# mlvm-strip

Library-backed ELF32/64 symbol/debug removal with explicit `-o OUTPUT`:

- Default / `--discard-all`: remove unreferenced locals; pin relocation targets
  and group signatures.
- `--strip-debug`: remove `.debug*`, `.zdebug*` and `.stab*` sections.
- `--strip-all`: remove every symbol from an input without relocations/groups.

Shares transactional section selection/renaming with `mlvm-objcopy` and verifies
through Limestone before/after edits. Inputs, including aliases and response
files, are protected against overwrite. See [TOOLS.md](../../docs/TOOLS.md).
