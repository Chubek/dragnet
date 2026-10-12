# 32. Archives and symbol extraction

[Contents](README.md) · [Next: Inspection](33-inspection.md)

Archives package member bytes. Linkers select members according to symbol
requirements; creating an archive does not itself link or merge its objects.

## Commands available in the baseline

```sh
build/mlvm-ar q build/answer.o -o build/libanswer.a
build/mlvm-ar t build/libanswer.a
```

Baseline `q` creates a deterministic archive rather than appending to an existing
one. It uses zero timestamps and owner/group IDs, has no index, and requires
unique simple names of at most 15 characters. The static linker can read regular
GNU/BSD archives, including long-name/index metadata, and derives extraction
needs from decoded member symbols.

## Library-backed archive editing

With `MLVM_ENABLE_BINUTILS=ON`, commands additionally support replacement,
deletion, printing, and extraction:

```sh
build/mlvm-ar r build/libanswer.a replacement.o -o build/updated.a
build/mlvm-ar d build/updated.a answer.o -o build/reduced.a
build/mlvm-ar p build/libanswer.a answer.o > build/extracted.o
mkdir -p build/extracted
build/mlvm-ar x build/libanswer.a --output-dir build/extracted
```

Use actual replacement files for the second workflow. `r` replaces matching
basenames in place and appends new ones; `d` deletes named members. Writes use a
distinct output archive and rebuild a deterministic GNU symbol index.

Member bytes are preserved exactly, including non-ELF payloads. Global/weak ELF
definitions contribute index entries; non-ELF members do not. Invalid ELF payloads
fail indexing. `t`, `p`, and `x` require valid archive framing but need not decode
all members as objects.

Extraction requires an existing output directory and validates selected names
and destinations before writing. Missing requested members fail. Thin archives
are unsupported. `p` writes binary stdout, so redirect it to a file when
extracting object bytes.

Reference: [indexed archive contract](../docs/TOOLS.md#indexed-archives).
