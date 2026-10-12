# README

Compiler/IR/assembler and baseline static-link dispatch, response files and
diagnostics. When `MLVM_ENABLE_BINUTILS` is enabled, binary utilities have dedicated
command implementations under `tools/`, sharing `tools/common` services. The
`tools/mlvm-ld` command selects the explicit-contract raw-link adapter or delegates
the static ELF/relocatable path to this driver. See `docs/TOOLS.md`.
