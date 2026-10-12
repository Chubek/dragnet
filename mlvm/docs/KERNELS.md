# Blackstone kernels in MLVM

Build with `-DMLVM_ENABLE_OPTILISP=ON` and link C++ consumers to
`MLVM::OptilispPass`. This uses the existing sfsexp reader and Blackstone registry.
`AUTO` enables the integration when sfsexp is available. The installed package
includes the original catalog and registry in `share/mlvm/blackstone`.

## Apply existing kernels

`examples/blackstone.mlvm` contains an application request:

```text
mlvm 1 {
  apply "blackstone.hir.constant-folding.integer-mul";
  block() {
    %a:"i32" = "arith.constant"() ["value"="6"];
    %b:"i32" = "arith.constant"() ["value"="7"];
    %c:"i32" = "arith.muli"(%a, %b) [];
  }
}
```

Run `build/mlvm examples/blackstone.mlvm --passes=dce`. Kernel requests execute
in their written order before `--passes`; the example returns a `42` constant.
`--print` and `--verify` retain directives without executing them (`--verify`
checks generic SSA IR). Successful execution consumes `apply` requests, so its
output does not implicitly schedule the same transformations again.

Use `--apply-kernel=ID` to append requests on the command line. The driver searches
the source-tree catalog, then its configured installation directory. Override
with `--blackstone-dir=DIR`, especially after a relocated `cmake --install
--prefix`. The directory contains `kernels` and either `optilisp/stdlib` (source
layout) or `stdlib` (installed layout). Kernel identities come from declarations,
not filenames. Selected upstream records are validated individually; defects in
unselected records do not authorize or prevent the selected transformations.

## Author custom kernels

`examples/kernels/and-self.olisp` is a complete custom kernel. It uses the
established Optilisp grammar, metadata, and first-class documentation. Its central
contract is:

```lisp
(transform (rewrite (bitwise-and x x) x))
```

Repeated variables match identical SSA definitions, including block arguments.
The backend checks the integer identity before replacing uses and erasing the
redundant operation. Operand-producing computations remain present.

Load this file with:

```sh
build/mlvm input.mlvm --kernel-file=examples/kernels/and-self.olisp \
  --apply-kernel=blackstone.hir.custom.and-self
```

Both options are repeatable. `--kernel-file` embeds the source into the module;
`--print` emits a self-contained textual module. Textual authoring uses
`kernel "COMPLETE OPTILISP SOURCE";` before any blocks, with the IR string escapes
for quotes and newlines. Definitions may contain multiple kernels. Requests refer
to their stable IDs. Duplicate definitions and unresolved IDs fail diagnostically.

## C++ API

```cpp
#include <mlvm/optilisp/pass.hpp>

// Module and Context already contain verified builtin SSA IR.
module.kernel_sources.push_back(custom_optilisp_source); // optional
module.kernel_applications.push_back("blackstone.hir.custom.and-self");
bool ok = mlvm::optilisp::run_kernels(
    module, blackstone_root / "optilisp/stdlib",
    blackstone_root / "kernels", diagnostics);
```

The full request batch runs on a private module copy and commits only after every
pass succeeds and verifies. Failure leaves both IR and application requests
intact. Diagnostics persist. Declarations remain in the successful output.
Definitions are validated even when there are no application requests.

For host-defined dialects or more advanced transformations, use
`Loader::load_source`/`load`, `ModuleOptimizer`, and `kernel_pass`. Implement
`supports`, stage identification, fact queries, and candidate-specific proofs and
execution. Register each consumed analysis with `PassManager::analyses`; analysis
values must own their data. Unknown evidence blocks application. The pass manager
provides verification, rollback, and analysis invalidation. See
`optilisp/README.md` for tree and NodeIR graph backends.

## Execution and validation contracts

The generic backend treats verified builtin integer SSA operations as the HIR
adapter domain. It supports `add`, `sub`, `mul`, `bitwise-and`, `bitwise-or`, and
`bitwise-xor` constructors, `(integer LITERAL-OR-VARIABLE)` constant patterns,
operand variables, and repeated-variable identity matching. `fold` results must
compute the same operator from the same literal bindings. Arithmetic is modular
at the actual `i1` through `i64` width; it never uses overflowing signed host
arithmetic. `rewrite` results currently forward bound SSA operands only, and each
candidate must satisfy an implemented integer identity. Arithmetic operations with
extra attributes are conservatively left untouched.

Supported positive predicates are `operation-has-defined-compile-time-semantics`,
`result-representable-under-integer-semantics`,
`operand-has-compatible-integer-type`, `addition-has-identity-zero`, and
`multiplication-has-identity-one`. Supported forbidden predicates are
`operation-has-observable-side-effects` and `signed-overflow-traps`. Requirements
may name `typed-expressions`, `integer-constant-operands`,
`integer-overflow-semantics`, and `constant-value-analysis`. Correctness obligations
are `preserve-observable-behavior` and `respect-integer-overflow-semantics`.
Candidate matching, builtin verification, and identity checks establish these
contracts in this restricted adapter domain. Unknown contracts, other stages,
history-dependent kernels, and unsupported transformations produce an explicit
backend diagnostic. Nonmatching candidates are normal successful no-ops.

`Loader::Profile::strict` remains the default supplemental validation profile.
The explicit `blackstone` profile accepts the original schema's six documentation
topics and optional analysis contract. When absent, declared analysis requirements
are consumed, every registered analysis is invalidated, and none is preserved.
No missing proof, capability, or reference is invented. Syntax, duplicate fields,
stage compatibility, contradictions, and reference checks remain enforced.
This compatibility profile does not repair the catalog's known reference and
stage defects. Tests provide execution evidence for the implemented integer
subset, not a general semantic proof for every catalog entry.
