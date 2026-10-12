# Bin2Bin

Bin2Bin consumes explicit Metacode binary contracts for decoding, lifting,
translation, encoding, object containers/linking, and runtime caching. Link `Limestone::bin2bin` for
`bin2bin.hpp`/`runtime.hpp`, or `Limestone::bin2bin_codegen` for Scheduler IR
encoding. Bindings include explicitly named fixed definition/use operands; these
check allocated physical registers without manufacturing encoding fields.
C architecture/buffer APIs are in `limestone/limestone.h`; owning C
runtime handles/callbacks are in `limestone/runtime.h` (link `Limestone::core`).
`Limestone::bin2bin_object` / `Limestone::object` expose owning ELF32/ELF64 objects and
metadata-driven symbol relocation/linking; see [the object-file guide](OBJECTS.md).

## Codecs and semantics

Supported metadata models are operand-free `fixed8` instructions and explicit
`masked` forms of 8--64 bits. Masked forms supply endianness, fixed bits,
nonoverlapping fields, register-number/name tables, and signed, unsigned,
register, or PC-relative operands. PC-relative fields preserve scale and
start/end-base policy. Forms must describe all bits and have unambiguous,
prefix-free boundaries; variable-length native formats need a codec adapter.

`tests/fixtures/masked-source.isa` and `masked-target.isa` are complete codec
examples. `backend-machine.isa` additionally supplies selection, allocation,
spilling, control, and field-to-operand encoding bindings.

```cpp
auto architecture = limestone::bin2bin::from_metacode(metadata);
if (!architecture) return architecture.error();
auto instructions = limestone::bin2bin::decode(architecture.value(), bytes, address);
auto cfg = limestone::bin2bin::analyze(architecture.value(), bytes, address);
```

Translatable forms need `op.tooling.binary_translation` semantic-equivalence
contracts; an optional status explicitly restricts support. Semantic operands
are bound through SExprTk; coincidence of opcode names/numbers is insufficient.
`lift` and `decompile` produce semantic assembly. Unsupported/fallback/privileged/
ambiguous statuses remain visible and are rejected by translation and CFG
analysis. Both codecs retain control-flow classifications, including returns,
indirect exits and traps; fixed8 direct targets need an operand codec.

`translate` uses matching explicit execution/state models, semantic operand
matching, instruction-boundary checks, relocated block targets, and monotonic
short-to-long branch relaxation. Source and target may be identical. Forms are
chosen deterministically by width and stable ID. Broader cross-ISA ABI/state
transformations require IL adapters. `TranslationOptions::semantic_transform`
admits a versioned host semantic adapter before target matching. It operates on
owning `LiftedInstruction` semantics with source address, status, control and
direct-target information. Instruction boundaries, control classifications and
direct branch targets are retained; CFG-changing optimization needs a region
adapter. Tunah supplies the checked equality-saturation adapter described below.

## Lifted-semantic equality saturation

Link `Limestone::tunah_bin2bin` and include `<tunah/binary_adapter.hpp>`. The host
declares the ISA's semantic vocabulary and proved equivalences in a Tunah session,
then supplies a legality analysis for input and extracted terms:

```cpp
limestone::tunah::BinaryAdapterOptions adapter;
adapter.costs.operators = {{"add", 20}, {"sub", 1}};
adapter.legality = validate_target_semantic_term;
auto transform = limestone::tunah::binary_transform(session, "state-model:analysis-v1", adapter);
if (!transform) return transform.error();
limestone::bin2bin::TranslationOptions options;
options.semantic_transform = std::move(transform.value());
auto bytes = limestone::bin2bin::translate(source, target, input, &cache, options);
```

Legality analysis establishes widths/types, architectural effects and operand
legality. Rules must be true under that state model. The adapter snapshots the
session and preserves source-located diagnostics; failed legality or extraction
does not publish translation/cache output. It does not infer arithmetic semantics
from mnemonic spelling. Generic Tunah terms support signed 64-bit integer literals;
larger unsigned semantic literals need a typed adapter.

## Encoding the compiler result

`encode_region` consumes Scheduler IR, final order, register names, allocation,
and explicit bindings. It verifies sequential hazards and CFG layout, checks
allocation completeness and explicit binding of every selected value/immediate,
and emits branch-relaxed bytes. Tied/early/control/effect
contracts remain explicit. Backend named relocations can be packaged through
`limestone::make_object` and linked using explicit object target metadata;
physical ABI lowering remains a backend responsibility. The top-level `make_target` adapter uses the final materialized
region/allocation after spills.

## Cache and runtime

`TranslationCache` provides memory caching. `open_cache` enables LMDB when built;
opening errors do not silently downgrade to memory storage. Cache identity includes
description/ISA/semantic/encoding data, source bytes, source/target addresses,
rule version, optimization/translation/plugin/runtime configurations, and semantic
adapter identity. Tunah's identity includes rule/operator definitions, costs and
budgets plus a mandatory host semantic/analysis identity. Time-limited or
cancellable Tunah transformations bypass translation-result caching. Generic host
transformers must identify every relevant configuration and analysis version and
mark nondeterministic transforms noncacheable.
Noncacheable transforms also bypass resident-region reuse in `Runtime::prepare`:
each observation reruns the transform and prepares a new immutable byte/code
view while retaining the region's heat counter.

`Runtime` owns source/target descriptions, an observation clock, heat counters,
bounded resident regions, and a translation cache. `prepare` translates an
addressed region, then invokes `CodeInstaller` when it reaches the configured
hot threshold. The installer returns an **owning callable** that retains executable
memory and host execution state. `invoke` retains that owner through the active
call, including self-invalidation. No host code is inferred from a bytecode
execution domain.

Published handles own bytes and distinguish cold/compiled/invalidated states.
Invalidation reaches retained handles, not just resident cache entries; handles
also expire when the runtime is destroyed. Installation detects recursive entry
and invalidation during translation and installation callbacks. Structured host
errors retain their status at both installation and invocation. Calls are synchronous; synchronize use of a
shared runtime. Executable installation, calling conventions, guest state,
fallback interpretation, and deoptimization belong to the embedding runtime.

### C runtime lifecycle

```c
#include <limestone/runtime.h>

limestone_runtime_options options;
limestone_runtime_options_default(&options);
options.hot_threshold = 2;
limestone_binary_runtime *runtime = limestone_runtime_create(
    source, target, &options, installer, host_state, &error);
if (!runtime) return report_error(&error);
limestone_translated_region *region = limestone_runtime_prepare(
    runtime, guest_bytes, guest_size, guest_address, target_address, &error);
if (!region) {
    limestone_runtime_destroy(runtime);
    return report_error(&error);
}
/* Repeated preparation heats identical bytes and addresses. Cold handles keep
 * their immutable cold state; a later prepare returns the compiled view. */
if (limestone_translated_region_is_compiled(region)) {
    int64_t result;
    limestone_status status = limestone_runtime_invoke(runtime, region, &result, &error);
    if (status != LIMESTONE_OK) report_error(&error);
}
limestone_translated_region_destroy(region);
limestone_runtime_destroy(runtime);
```

The host provides `source`, `target`, `installer`, guest bytes/addresses and
diagnostic handling. A NULL installer supports translation and observation.
Installer callbacks receive borrowed guest/translated byte views and a zeroed
`limestone_runtime_executable`. Supply `execute`, optional `release`, and userdata;
`release` runs exactly once after the final owning handle/call. Records are adopted
on installation failure, empty execute callbacks, and invalidation too. Cold and
compiled handles may coexist. Eviction removes residency without invalidating
retained handles. Overlap invalidation reaches all published views; its returned
count counts views rather than unique byte regions. Failure preserves result/count
output storage and returns the callback status and bounded diagnostic.

Callbacks may invalidate code. Preparation and cache reconfiguration reject
recursive preparation; an active runtime must outlive all its synchronous calls.
Executable releases run after residency mutations finish, so release callbacks
can invalidate other code safely. Calls during runtime destruction return
conflict; byte inspection on retained region handles remains available.
`limestone_translated_region_get_view` remains readable after invalidation or
runtime destruction. Its spans remain borrowed until region-handle destruction.
Destroy functions accept NULL. `limestone_runtime_open_cache` opens explicit LMDB
storage with the same no-fallback behavior as the C++ cache API.

CLI modes are `--disassemble ISA`, `--decompile ISA`, and
`--translate SOURCE TARGET [--cache DIRECTORY]`, taking raw binary input and
optional `-o FILE`. `--inspect-object`, `--wrap-object`, `--link-objects` and compiler
`--object SYMBOL` use the ELF32/ELF64 object layer. Higher-level/LLM decompiler plugins
remain separate adapters.
