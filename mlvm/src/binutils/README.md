# Binary utilities library

`MLVM::Binutils` exposes `include/mlvm/binutils.hpp`:

- `adapter.cpp`: owning ELF/section/symbol/relocation/group views and raw linking
  over read-only Limestone Bin2Bin, with cumulative resource budgets.
- `archive.cpp`: byte-preserving payloads and remapped library-generated GNU indexes.
- `contract.cpp`: strict versioned target/relocation TOML using vendored tomlc99.
- `transform.cpp`: transactional object edits and streaming string callbacks.

Dedicated binary command implementations use this library through `tools/common`.
See `docs/TOOLS.md` for format limits and `docs/API.md` for ownership. Configure
with `MLVM_ENABLE_BINUTILS=AUTO|ON|OFF`; no libstone files are regenerated or patched.
