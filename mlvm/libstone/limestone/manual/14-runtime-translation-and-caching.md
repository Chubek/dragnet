# Chapter 14. Runtime Translation and Caching

[Previous: Objects and linking](13-object-files-and-linking.md) · [Contents](README.md) · [Next: TraceML](15-traceml-and-metatracing.md)

## 14.1 A synchronous translation runtime

Bin2Bin's runtime observes addressed guest byte regions, translates them, tracks
heat, optionally installs hot code, and manages bounded residency. It supplies
the translation/ownership boundary an embedder can use in an interpreter, emulator,
or JIT-oriented execution system.

The runtime is synchronous. Region formation, guest-state interpretation,
fallback execution, calling conventions, executable memory, and deoptimization
belong to the embedding application. A bytecode execution domain is never
automatically invoked as native host machine code.

The C++ API is [bin2bin/runtime.hpp](../bin2bin/runtime.hpp), exported by
`Limestone::bin2bin`. The C runtime API is
[limestone/runtime.h](../limestone/runtime.h), available through `Limestone::core`.

## 14.2 Observation, heat, and preparation

`RuntimeOptions` defaults to hot threshold 10 and maximum 1,024 resident regions.
Both must be positive. The runtime owns copied source/target descriptions,
translation options, observation clock, heat counters, resident entries, and
translation cache.

`prepare(bytes,guest_address,target_address)` copies guest input and translates
it. Identical bytes and addresses share heat. Changed code or changed addresses
are a different translation identity. When a region reaches the hot threshold,
the runtime invokes an optional installer.

```cpp
limestone::bin2bin::RuntimeOptions options;
options.hot_threshold = 2;
limestone::bin2bin::Runtime runtime(source, target, options);
auto first = runtime.prepare(guest_bytes, 4096, 8192);
auto second = runtime.prepare(guest_bytes, 4096, 8192);
```

This fragment assumes explicit source/target architectures and input bytes.
Without an installer, preparation still translates and observes heat. It does
not produce an executable callable merely because the threshold was reached.

## 14.3 Immutable published region views

`TranslatedRegion` owns guest address, target address, guest bytes, translated
bytes, validity state, and optional executable ownership. Published views are
immutable snapshots of the cold or compiled state at publication.

If a cold handle is retained and a later observation installs hot code, the old
handle stays cold. A new preparation returns the compiled view. Applications
must inspect the returned handle rather than assuming all retained views changed
in place.

Resident eviction removes cache residency without invalidating retained views.
Explicit invalidation affects overlapping published handles, including evicted
ones. Runtime destruction expires validity, while byte inspection remains
available through an owning region handle.

These are independent questions:

- Is the region's byte snapshot still owned?
- Is it valid for this runtime's current guest code?
- Does it have a compiled callable?
- Is it currently resident?

A region can own compiled bytes and executable state while no longer being valid
for invocation.

## 14.4 Installer and callable ownership

A C++ `CodeInstaller` receives a translated region and returns an owning callable
of type `std::function<Result<int64_t>()>`. Captures must retain executable memory
and any host state needed through active calls. Returning a function that refers
to a temporary mapping violates the installer contract.

The installer owns platform execution policy: memory allocation and permissions,
target-address agreement, calling convention, guest/host state, instruction-cache
maintenance, and fallback/deoptimization integration. The runtime owns the
returned callable's lifetime and validity checks.

`invoke` retains the executable owner throughout the synchronous call. A call
may invalidate its own region without freeing the code while it is executing.
After the call, the last remaining owner can release it.

Target addresses are explicit translation inputs. If code depends on relocation
base, the installer must place it at the agreed address or supply a compatible
address/position-independent contract before preparation. Raw bytes do not carry
an implicit host mapping.

## 14.5 The C executable record

The C installer receives borrowed guest/translated byte views and a zeroed
`limestone_runtime_executable`:

```c
typedef struct limestone_runtime_executable {
  limestone_runtime_execute execute;
  limestone_runtime_release release;
  void *userdata;
} limestone_runtime_executable;
```

A successful record needs `execute`. `release` is optional for borrowed userdata;
when supplied, it runs exactly once after the last owning view/active call.
Installer userdata itself stays borrowed until runtime destruction.

Every returned executable record is adopted, including failed installation,
missing execute callback, or invalidation during installation. This differs from
optimizer callback registration, which adopts userdata only on success. The
installer can allocate ownership and return an error without leaking its record.

Execution callbacks write their integer result on success and report structured
status/diagnostic on failure. The runtime preserves callback error categories.
Caller result storage is unchanged when invocation fails.

## 14.6 A C observation lifecycle

```c
#include <limestone/runtime.h>

/* source and target are previously loaded architecture handles. */
limestone_error error;
limestone_runtime_options options;
limestone_runtime_options_default(&options);
options.hot_threshold = 2;

limestone_binary_runtime *runtime = limestone_runtime_create(
    source, target, &options, NULL, NULL, &error);
if (!runtime) return 1;

limestone_translated_region *region = limestone_runtime_prepare(
    runtime, guest_bytes, guest_size, 4096, 8192, &error);
if (!region) {
  limestone_runtime_destroy(runtime);
  return 1;
}

limestone_runtime_region_view view;
if (limestone_translated_region_get_view(region, &view, &error) != LIMESTONE_OK) {
  limestone_translated_region_destroy(region);
  limestone_runtime_destroy(runtime);
  return 1;
}
/* view bytes are borrowed from region until its destruction. */
limestone_runtime_destroy(runtime);
/* Region bytes remain inspectable; validity is now false. */
limestone_translated_region_destroy(region);
```

This is an embedding fragment with caller-supplied architecture handles and guest
bytes. Runtime creation copies architecture/options. Retained region handles
are independent owners; destroy every returned handle once.

## 14.7 Invalidation and self-modifying guest code

`invalidate(guest_address,byte_count)` reaches every overlapping published view.
The returned count is a number of invalidated views, not unique guest regions.
Cold and compiled snapshots can both contribute.

An embedding VM should invalidate affected translations when guest code changes.
Re-preparing changed bytes also creates a new identity, but previously retained
handles remain separately owned; explicit invalidation expresses that they may
no longer execute the old code.

Invalidation uses checked address ranges. C count storage is written only on
success. Bytes remain readable after invalidation, supporting diagnostics and
historical inspection without permitting stale invocation.

## 14.8 Reentrancy and destruction boundaries

Callbacks may invalidate code and invoke existing regions. Recursive preparation
and cache reconfiguration during active preparation are rejected. Translation
and installation detect recursive entry and invalidation during their callbacks.

Executable releases run after residency mutations complete, so releases can
invalidate other code without observing a half-mutated cache. Operations during
runtime destruction return conflict. An active runtime must outlive every
synchronous operation; destroying it from its active callback is outside the
contract.

Independent runtimes are reentrant. Shared-runtime access and shared handle
destruction need embedding synchronization. The presence of owning snapshots
does not turn a mutable runtime into a general concurrent scheduler.

## 14.9 Memory and persistent translation caches

`TranslationCache` provides in-memory entries. `open_cache` opens LMDB storage
when enabled; runtime equivalents are `open_persistent_cache` and
`limestone_runtime_open_cache`.

```sh
build/limestone-cli --translate tests/fixtures/byte-source.isa \
  tests/fixtures/byte-target.isa /tmp/opencode/inc.bin \
  --cache /tmp/opencode/counter-cache -o /tmp/opencode/increment.bin
```

Opening errors do not silently downgrade to memory caching. A requested persistent
cache is an explicit storage contract. Default map size is 64 MiB, with API
configuration where exposed.

A cached result must represent the same translation problem. Identity includes
description/ISA/semantic/encoding data, source bytes, addresses, framework/schema
versions, rule version, optimization/translation/plugin/runtime configuration,
and semantic adapter identity. A mnemonic string alone cannot identify a target.

## 14.10 Semantic optimizer identities and cacheability

Tunah binary transforms encode operator/rule definitions, conditions, costs,
and budgets automatically. The host context identity versions semantic analysis,
legality proofs, and any external assumptions. Change that identity when the
meaning or analysis behavior changes.

Generic host transforms must identify every behavior-relevant configuration.
Nondeterministic transforms should declare themselves noncacheable. Time-limited
or cancellable Tunah transformations automatically bypass translated-byte caching.

Noncacheable transforms also bypass resident-region reuse on every observation.
Preparation reruns the transform and publishes a fresh byte/code view, while
retaining the observation heat counter. A resident hit must not skip a requested
cancellation check or reuse a result from a different timing outcome.

Runtime creation snapshots a C binary transform. The original transform/session
can be destroyed independently, while retained callback ownership survives until
the final snapshot is released.

## 14.11 VM integration pattern

A typical interpreter integration has a host-owned region table and guest-state
model. At a region entry, the VM identifies current bytes and addresses, prepares
the region, invokes a valid compiled handle when available, and otherwise uses
its normal execution path. Code mutation invalidates retained views. Installation
captures the VM/ABI state needed by the callable.

Heat is an observation counter, not a full profiler. Trace formation, exits,
continuations, cross-region linking, and guest-state transfer must be explicitly
defined by the embedding system. VMWeave hooks can provide observation points
without choosing those policies themselves.

## 14.12 Testing runtime behavior

Test cold/hot transitions, copied architectures/options, retained cold views,
resident eviction, overlap invalidation, self-invalidation, failed installers,
empty execute records, exactly-once release, and callback error propagation.
Also test noncacheable transforms across repeated observations, ensuring heat
continues while translation is rerun.

Native tests should use the actual installation base and retain mapped memory
through invocation. Bytecode tests can install an owning interpreter callable
instead. Both validate the runtime's ownership contract without conflating guest
bytes with host executable code.

[Previous: Objects and linking](13-object-files-and-linking.md) · [Contents](README.md) · [Next: TraceML](15-traceml-and-metatracing.md)
