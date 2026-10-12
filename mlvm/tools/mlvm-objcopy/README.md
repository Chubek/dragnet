# mlvm-objcopy

Copies ELF32/64 relocatable objects in either byte order through `MLVM::Binutils`
and Limestone. Repeatable `--remove-section=NAME`, `--only-section=NAME` and
`--rename-section=OLD=NEW` are transactional and remap symbols/relocations/groups.
Retained references to removed definitions fail. `--discard-all`, `--strip-debug`
and `--strip-all` select explicit stripping policies.

`-O binary` requires one selected file-backed section without relocations.
`-o OUTPUT` is required and must differ from the input. ELF serialization is
canonical; section semantics and metadata are preserved. See
[TOOLS.md](../../docs/TOOLS.md) for policy restrictions and availability.
