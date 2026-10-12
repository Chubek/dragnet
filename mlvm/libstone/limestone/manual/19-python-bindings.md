# Chapter 19. Python Bindings

[Previous: C/C++ embedding](18-c-and-cpp-embedding.md) · [Contents](README.md) · [Next: CLI and languages](20-cli-and-textual-languages.md)

## 19.1 Python over the public C ABI

The optional Python module uses SWIG to wrap the `limestone_*` C APIs. Convenience
classes add unique ownership, context managers, copied inspection properties,
and `LimestoneError` exceptions. Algorithms and target semantics remain in the
same native libraries used by C/C++ consumers.

The interface source is [bindings/limestone.i](../bindings/limestone.i), and the
component guide is [bindings/README.md](../bindings/README.md). Generated wrapper
code lives in the build tree. The module does not expose private solver/e-graph
types or supply automatic Python proof callbacks for native function-pointer APIs.

## 19.2 Build, import, and installation

```sh
cmake -S . -B build-python -DLIMESTONE_BUILD_PYTHON_BINDINGS=ON
cmake --build build-python -j 4
ctest --test-dir build-python -R python-bindings --output-on-failure
PYTHONPATH=build-python/bindings/python python3
```

Configuration requires SWIG 4 and Python interpreter/development-module headers.
Use the interpreter selected during configuration. The extension is a native
module built for that Python environment.

The helper `bindings/generate-bindings.sh BUILD_DIRECTORY [CMAKE_OPTIONS...]`
configures and builds the module. With the usual GNU layout, installation places
it in `lib/limestone/python`; `LIMESTONE_PYTHON_INSTALL_DIR` overrides the relative
destination. Add the actual installed directory to `PYTHONPATH`.

The installed-consumer test exercises import and execution after relocating a
prefix. It validates the module's installed library/package boundary rather than
depending on a source-tree Python path.

## 19.3 Unique owning handles

Convenience owners support `with` and idempotent `close()`. Copy and deep-copy are
rejected because they would create ambiguous unique-handle ownership. Access
after close raises `ValueError`.

```python
import limestone

with limestone.compile_source("(add 20 22)") as module:
    text = module.text
    exchange = module.exchange
    stages = module.stages
print(text)                   # Python-owned strings remain usable.
print(stages)
```

Text and byte properties return Python-owned copies. A selection/result handle
can also outlive the document/session that produced it. The copied inspection
data and independently owning native result are related but distinct conveniences.

Checked operations raise `LimestoneError`, whose `code` retains the C status and
whose message is the structured native diagnostic. Do not confuse closed-handle
programming errors with a target selection failure.

## 19.4 Source and UMD compilation

`compile_source` evaluates a closed TraceML integer computation and produces
portable machine-oriented output. `compile_umd` consumes machine/graph text;
`compile_umd_file` resolves bounded relative includes.

```python
with limestone.compile_umd_file("tests/fixtures/arithmetic-includes.umd") as module:
    print(module.text)
    print(module.exchange)
```

Text constructors do not implicitly use the filesystem to resolve includes.
Choose a file constructor when the document depends on relative source files.
The same cumulative include limits and source-cycle diagnostics apply as in C++.

`Module.bytes` copies encoded output when present. `Module.stages` lists actual
pipeline stages. Portable source compilation does not invent an executable
target or ABI.

## 19.5 Explicit targets through raw handles

`compile_target` accepts raw target/configuration handles. The current convenience
layer does not define a separate owning compiler-target class. Manage these raw
owners explicitly:

```python
from pathlib import Path
import limestone

error = limestone.limestone_error()
target = limestone.limestone_target_load_isa(
    Path("tests/fixtures/native-constant.isa").read_text(), error)
if not target:
    raise limestone.LimestoneError(error)
configuration = None
try:
    configuration = limestone.limestone_configuration_create()
    if not configuration:
        raise MemoryError("configuration allocation failed")
    status = limestone.limestone_configuration_set_pipeline(
        configuration, 1, 1, 1, 1, 0, error)
    if status != limestone.LIMESTONE_OK:
        raise limestone.LimestoneError(error)
    status = limestone.limestone_configuration_set_algorithms(
        configuration, limestone.LIMESTONE_SELECT_BURS,
        limestone.LIMESTONE_ALLOCATE_COLOR, error)
    if status != limestone.LIMESTONE_OK:
        raise limestone.LimestoneError(error)
    with limestone.compile_target("(add 20 22)", target, configuration) as module:
        encoded = module.bytes
        print(module.stages, len(encoded))
finally:
    limestone.limestone_configuration_destroy(configuration)
    limestone.limestone_target_destroy(target)
```

This uses the bounded native fixture's explicit return ABI and immediate legality.
The bytes are ordinary Python bytes; installing/invoking them remains a host
execution contract.

## 19.6 Unisel model and selection inspection

```python
with limestone.UniselDocument.from_file(
        "tests/fixtures/arithmetic-includes.umd") as document:
    model = document.analyze()
with model:
    print(model.candidates)
    print(model.clauses)
    selection = model.select(algorithm=limestone.LIMESTONE_SELECT_GLOBAL)
with selection:
    print(selection.cost, selection.matches)
    handoff = selection.text
```

Candidate dictionaries include pattern/root IDs, cost, name/opcode/origin/reason,
and covered/input/output identities. Clause lists use signed one-based candidate
literals. An uncovered graph can produce an inspectable empty coverage clause;
selection then raises `LIMESTONE_UNSATISFIABLE`.

The result owns matches in emission order and canonical Scheduler IR. Closing
the document before model inspection and the model before result inspection is
valid by design.

## 19.7 BURS states and attempts

```python
with limestone.BURSDocument.from_file(
        "tests/fixtures/selection.limeburg") as document:
    analysis = document.analyze()
    selection = document.select()
with analysis:
    print(analysis.states)
    print(analysis.attempts)
with selection:
    print(selection.cost)
    handoff = selection.text
```

States include node, nonterminal ID/name, rule, and cost. Attempts include node,
rule, optional cost, state-improvement flag, and reason. A failed match has cost
`None`. An improvement flag records the attempt's historical update, not a
promise that the final root selection uses it.

Analysis is available for well-formed no-derivation trees. If selection raises,
retain the independent analysis owner for diagnostics and close it normally.

## 19.8 Scheduling and allocation

```python
with limestone.SchedulingDocument(handoff) as document:
    schedule = document.schedule()
with schedule:
    print(schedule.issues)
    print(schedule.groups)
```

Issue tuples contain instruction, cycle, optional slot, and chosen resources.
Groups expose ordered members and retained grouping/fusion information. For
real target scheduling, supply a document containing the explicit machine model;
a standalone selection handoff does not establish measured hardware timing.

```python
from pathlib import Path

with limestone.AllocationDocument(
        Path("tests/fixtures/allocation.regtl").read_text()) as document:
    assignment = document.allocate(algorithm=limestone.LIMESTONE_ALLOCATE_COLOR)
with assignment:
    print(assignment.registers)
    print(assignment.spilled)
```

`allocate` defaults to unit zero/function zero. Use
`function=limestone.LIMESTONE_ALLOCATION_RANGES` for a unit's explicit range
problem. Spills are decisions; target materialization belongs to the compiler
pipeline.

## 19.9 Owning Tunah sessions and results

```python
with limestone.Optimizer(
        "(operator add 2)(rule zero (add ?x 0) ?x)") as optimizer:
    optimizer.set_cost("add", 3)
    optimizer.set_cost(None, 1)           # Literal local cost.
    optimizer.set_limits(nodes=1000, classes=1000, trace=True)
    optimized = optimizer.saturate("(add 42 0)", "example.term")
with optimized:
    print(optimized.expression)
    print(optimized.info)
    print(optimized.trace)
```

The session supports transactional rule loads, file loads, operator declarations,
costs, limits, and typed pure graph declarations. `define_graph_operator` followed
by `attach(raw_target_handle)` snapshots the optimizer into the target. Later
source mutation/destruction does not alter the attachment.

`binary_transform(context_identity,legality,userdata,release)` uses an explicit
native extension's C proof callback. `BinaryTransform` owns it and exposes
identity/cacheability. Predicate/cancellation/installer callbacks likewise use
the raw C ABI. Keep extension code loaded while any owning snapshot retains its
function pointer.

## 19.10 Binary architectures and observation runtimes

```python
from pathlib import Path

with limestone.BinaryArchitecture(
        Path("tests/fixtures/byte-source.isa").read_text()) as source:
    with limestone.BinaryArchitecture(
            Path("tests/fixtures/byte-target.isa").read_text()) as target:
        print(source.disassemble(b"\x01\x01", address=4096))
        translated = source.translate(b"\x01\x01", target=target,
                                      source_address=4096, target_address=8192)
        runtime = limestone.BinaryRuntime(source, target, hot_threshold=2)
with runtime:
    region = runtime.prepare(b"\x01\x01", 4096, 8192)
    print(region.valid, region.compiled, runtime.resident_count)
with region:
    print(region.bytes, region.guest_bytes)
    print(region.valid)                    # False after runtime destruction.
assert translated == b"\x09\x09"
```

Runtime creation copies architectures. The convenience runtime supplies no native
installer, so it supports translation and observation rather than automatic code
execution. `invoke` on a cold handle reports unsupported behavior. Native
installation requires a C callback integration.

`invalidate` counts invalidated published views. Retained region bytes remain
inspectable after invalidation/destruction. An optional transform is copied into
the runtime, with the same cacheability policy as C++.

## 19.11 Object builders and addressed images

```python
from pathlib import Path

with limestone.ObjectTarget(
        Path("tests/fixtures/native-constant.isa").read_text()) as target:
    with target.create() as object_:
        section = object_.add_section(".data", b"\0" * 8,
                                      flags=3, alignment=8)
        object_.add_symbol("value", section=section, size=8)
        serialized = object_.emit()
        print(object_.sections, object_.symbols)
        image = target.link([object_], base_address=4096)
with image:
    assert image.symbols["value"] == 4096
    print(image.base_address, image.bytes)
```

Here ELF flags `3` mean write/allocate. The example links data, not an executable
function. `ObjectFile(serialized,source_name)` loads the owning ELF snapshot.
Builders expose `add_relocation`; `from_code` and `from_module` package raw/encoded
compiler bytes. `link` accepts external symbol addresses and image limit, returning
an independent `LinkedImage`.

Sections, symbols, emitted bytes, and linked-image properties return copies.
The host still owns installation and ABI invocation.

## 19.12 Raw calls and boundary discipline

Raw functions retain C ownership. Destroy every returned raw owner exactly once,
and do not manually destroy a pointer owned by a convenience wrapper. Output
scalar pointers are returned as additional tuple fields. The status determines
whether outputs are meaningful, even when failed-call temporaries are initialized
to zero.

`UInt32Array` and `ByteArray` provide storage for raw spans; retain them through
the synchronous call. Memory descriptors retain copied address-space strings and
`set_alias_sets` storage. Borrowed output strings are read-only.

Python integration does not replace semantic proof, target facts, callback lifetime,
or synchronization. It makes those existing contracts accessible with predictable
ownership and useful copied inspection data.

[Previous: C/C++ embedding](18-c-and-cpp-embedding.md) · [Contents](README.md) · [Next: CLI and languages](20-cli-and-textual-languages.md)
