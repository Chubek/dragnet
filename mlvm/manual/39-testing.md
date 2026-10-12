# 39. Testing and troubleshooting

[Contents](README.md) · [Next: Extending MLVM](40-extending.md)

Configure with tests enabled, build, and inspect registered tests before choosing
a focused subset:

```sh
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'mlvm-(core|pass|conversion)' --output-on-failure
```

Optional-component tests exist only when their dependencies and targets are
enabled. Python integration tests are registered when a suitable interpreter is
found. Native execution checks are limited to supported Linux/x86-64 hosts.

## Where evidence lives

| Area | Test source |
| --- | --- |
| Contexts and SSA | `tests/core/` |
| Pass rollback and analyses | `tests/pass/pass.cpp` |
| Conversion routes | `tests/conversion/conversion.cpp` |
| Objects and relocation edges | `tests/object.cpp` |
| Broader binary models | `tests/binutils.cpp` |
| Tool status and end-to-end behavior | `tests/integration/toolchain.py` |
| Library-backed CLI | `tests/integration/binutils.py` |
| Kernel CLI | `tests/integration/kernels.py` |
| Optilisp and NodeIR | `optilisp/tests/` |

Catalog tooling can also run from the source directory:

```sh
python3 -B -m tools.blackstone validate
```

That validates MLVM's supplemental catalog; it does not prove arbitrary catalog
kernels executable by every backend.

## Troubleshooting guide

| Symptom | First check |
| --- | --- |
| Optional tool/target absent | CMake's resolved feature settings and dependency paths |
| Unknown operation | Dialect registration and operation spelling |
| Stale use list | Direct operand edits without `rebuild_uses()` |
| Kernel no-op | Pattern, stage, facts, and candidate conditions |
| Kernel execution error | Supported verb/contracts and selected registry |
| Installed catalog not found | Actual `--blackstone-dir` after prefix relocation |
| Undefined link symbol | Symbol definition, archive order, and strong/weak binding |
| Object rejected | Relocatable format subset and enabled binary-tool mode |

For regressions, capture the smallest input, exact command, version, build
features, and diagnostics. Canonical IR and deterministic objects make useful
reproduction artifacts. Add negative cases for malformed input and verify
rollback after partial edits, not just successful transformation output.

Reference: [test registration](../CMakeLists.txt), [Optilisp tests](../optilisp/tests).
