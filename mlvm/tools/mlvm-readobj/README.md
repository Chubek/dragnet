# mlvm-readobj

Inspects the shared owning object model over Limestone Bin2Bin: ELF32/64
relocatable objects in either byte order, including ELF archives. Select views
with `--file-header`, `-S`/`--sections`, `-s`/`--symbols`, `-r`/`--relocs`,
`--groups`, or `-a`/`--all` (default). Reports identity, logical NOBITS sizes,
symbol type/visibility and explicit-versus-implicit relocation addends.

See [TOOLS.md](../../docs/TOOLS.md) for library-backed build options and limits.
