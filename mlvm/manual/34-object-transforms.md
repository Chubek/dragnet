# 34. Copying and stripping objects

[Contents](README.md) · [Next: Raw linking](35-raw-linking.md)

`mlvm-objcopy` selects, removes, or renames sections and writes a verified object.
`mlvm-strip` applies symbol-removal policies. Always use distinct input/output
paths.

```sh
build/mlvm-objcopy build/answer.o --rename-section=.text=.code \
  -o build/renamed.o
build/mlvm-strip build/answer.o -o build/answer.stripped.o
```

## Selection and consistency

`--only-section=NAME` and `--remove-section=NAME` operate on original names.
In the library-backed tools, repeated `only` selections form a union and removal
takes precedence. Renaming happens afterward. Conflicting renames for the same
source name are errors.

Removing a section can invalidate a surviving relocation or group reference.
The transformation must reject that edit instead of leaving a dangling index.
Library-backed edits verify before and after, remap surviving indexes, and
commit transactionally through `transform_binary_object`.

## Strip policies

The baseline removes only unreferenced local symbols, retaining globals and
relocation-required symbols. Library-backed `--discard-all` provides that policy
and is the strip tool's default. It additionally supports:

- `--strip-debug`: remove `.debug*`, `.zdebug*`, and `.stab*` sections;
- `--strip-all`: remove all symbols, requiring no relocations or groups.

Combining distinct strip policies fails. Relocation targets and group signatures
remain pinned under conservative stripping.

## Raw extraction

`-O binary` extracts exactly one selected file-backed section with no retained
relocations. It does not perform multi-object linking or assign addresses.
NOBITS cannot be extracted as if it contained file bytes.

ELF output is canonically reserialized, preserving the semantic model rather
than original offsets or padding. For byte-exact packaging of complete objects,
use library-backed archives instead of an object rewrite.

Reference: [transformation contract](../docs/TOOLS.md#object-copying-and-stripping).
