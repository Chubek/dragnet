# 35. Target contracts and raw images

[Contents](README.md) · [Next: Cephyr frontend](36-cephyr-frontend.md)

Library-backed `mlvm-ld` can emit an addressed raw image using an explicit
version-1 TOML target contract. This requires `MLVM_ENABLE_BINUTILS=ON`.

```sh
build/mlvm-ld --target-config examples/targets/x86_64.toml -O binary \
  --base-address 0x1000 build/start.o build/libanswer.a \
  --map build/image.map -o build/image.bin
```

The contract declares object class, machine, endianness, flags, OS ABI, ABI
version, text alignment, and supported relocation fields. Start from the shipped
contract; changing the machine number alone does not define another target.

## Relocation fields

Each record gives a numeric type, name, absolute/PC-relative kind, storage width,
bit offset, field width, scale, and result signedness. Optional `pc_bias` applies
only to PC-relative fields. Applying REL records also needs
`implicit_addend_signed`, independently specifying input-addend interpretation.

With symbol address `S`, addend `A`, and patch address `P`:

- Absolute: `(S + A) / scale`.
- PC-relative: `(S + A - (P + pc_bias)) / scale`.

The linker checks arithmetic, field bounds, exact scale divisibility, overflow,
and overlapping patches. Unknown keys, duplicate fields, invalid widths, and
unsupported contract versions fail rather than acquiring inferred meanings.

## Layout and resolution

Allocated sections follow declaration order and preserve alignment and
permissions metadata; padding and NOBITS storage are zeroed. Root objects are
processed first, then archives are rescanned to a fixed point for unresolved
strong references. Weak undefined references resolve to zero without extraction.

`--defsym=NAME=ADDRESS` supplies explicit external addresses. The map records
section placements, sorted global symbols, and discarded groups. Map, image,
contract, response-file, and input paths must satisfy output-separation rules.

A raw image has no executable container, entry point, installed runtime, or
executable permissions. `-r` and entry selection are unsupported in this mode;
the resulting bytes need a consumer that understands the chosen address map.

Reference: [TOML schema and limits](../docs/TOOLS.md#metadata-driven-raw-linking),
[x86-64 contract](../examples/targets/x86_64.toml).
