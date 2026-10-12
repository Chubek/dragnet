# mlvm-ld

Static x86-64 ELF linking and `-r` relocatable linking use the baseline driver.
Regular GNU/BSD archives support long names and on-demand extraction.

Library-backed raw linking uses Limestone and vendored tomlc99:

```sh
mlvm-ld --target-config examples/targets/x86_64.toml -O binary \
  --base-address 0x1000 caller.o library.a --map image.map -o image.bin
```

ELF class, identity and relocation semantics come from the explicit TOML contract.
REL/RELA overflow, storage fields, alignment, strong/weak resolution and COMDAT
are checked. `--defsym=NAME=ADDRESS` supplies explicit externals. The raw image
owns bytes and has no executable container/entry point. See
[TOOLS.md](../../docs/TOOLS.md) for the contract grammar, limits and build options.
