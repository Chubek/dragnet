# 33. Inspecting objects and strings

[Contents](README.md) · [Next: Object transforms](34-object-transforms.md)

Inspect relocatable objects at each stage of a toolchain investigation. The
baseline tools offer narrow comprehensive views; library-backed tools add
selection/filter options and accept multiple objects or ELF archives.

## Baseline-friendly inspection

```sh
build/mlvm-nm build/answer.o
build/mlvm-readelf build/answer.o
build/mlvm-objdump -d build/answer.o
build/mlvm-size build/answer.o
build/mlvm-strings -n 4 build/answer.o
```

Use `nm` for definitions and undefined references, `readelf`/`readobj` for object
metadata, and `objdump` for bytes or the supported instruction subset. These are
MLVM output formats, not a promise of GNU option/output compatibility.

## Library-backed views

| Tool | Useful options |
| --- | --- |
| `mlvm-nm` | `--extern-only`, `--undefined-only`, `--defined-only`, `--numeric-sort` |
| `mlvm-readelf`, `mlvm-readobj` | `--file-header`, `--sections`, `--symbols`, `--relocs`, `--groups` |
| `mlvm-objdump` | `--section-headers`, `--full-contents`, `--syms`, `--relocs`, `-d`, `-j NAME` |
| `mlvm-size` | `-A`, `-B`, `--radix=d\|o\|x` |
| `mlvm-strings` | `-n LENGTH`, `-t d\|o\|x`, `-f`, `-e s\|l\|b` |

In this table, vertical bars indicate choices, not literal shell arguments.
For example, use `--radix=x` or `-t x`. `readelf -h` means help; use
`--file-header` for the header view.

Strings modes are ASCII and aligned UTF-16LE/BE ASCII code points, not general
Unicode decoding. The default minimum is four code points. EOF-terminated runs
are included, while incomplete code units are ignored.

Library-backed archive reports use `archive(member)` headings. Unknown
relocations remain inspectable. Non-x86 identities do not acquire disassembly
support merely because their ELF structure can be read.

Reference: [inspection options](../docs/TOOLS.md#inspection-and-strings).
