# mlvm-ar

With library-backed tools enabled, creates deterministic GNU archives with an ELF
symbol index and preserves member bytes exactly. Reads regular GNU/BSD archives,
including long names; arbitrary non-ELF members can be listed/extracted.

```sh
mlvm-ar q one.o 'long object name.o' -o library.a
mlvm-ar t library.a
mlvm-ar r library.a replacement.o -o updated.a
mlvm-ar d updated.a one.o -o reduced.a
mlvm-ar p library.a one.o > copy.o
mlvm-ar x library.a --output-dir existing-directory
```

Writes require a distinct output. Extraction rejects unsafe paths and duplicate
destinations. Indexing uses the read-only Limestone object/archive APIs through
MLVM adapters. See [TOOLS.md](../../docs/TOOLS.md) for limits and fallback behavior.
