# Python bindings

The optional SWIG interface wraps the public C APIs in `limestone/limestone.h`,
`limestone/il.h`, `limestone/runtime.h`, `limestone/optimization.h`, and
`limestone/object.h`. It requires SWIG 4 and Python interpreter/development-module
headers. Generated C++/Python files live in the build tree.

```sh
cmake -S . -B build -DLIMESTONE_BUILD_PYTHON_BINDINGS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure -R python-bindings
PYTHONPATH=build/bindings/python python3
```

`sh bindings/generate-bindings.sh BUILD_DIRECTORY [CMAKE_OPTIONS...]` configures
and builds just the module. With the default GNU layout, installation puts it in
`lib/limestone/python`; override `LIMESTONE_PYTHON_INSTALL_DIR` if needed. Add that
directory to `PYTHONPATH` for the Python version used during the build.

```python
import limestone

with limestone.compile_source("(add 20 22)") as module:
    print(module.text)
    print(module.exchange)

# A raw Infobank target and extended configuration may also be supplied:
# with limestone.compile_target(source, target_handle, config_handle) as module:
#     encoded_bytes = module.bytes

with limestone.BURSDocument(rule_source) as document:
    with document.analyze() as analysis:
        print(analysis.states, analysis.attempts)
    selection = document.select()
with selection:                         # results outlive their documents
    handoff = selection.text
with limestone.SchedulingDocument(handoff) as document:
    with document.schedule() as schedule:
        print(schedule.issues)
        print(schedule.groups)

with limestone.AllocationDocument(regtl_source) as document:
    with document.allocate(algorithm=limestone.LIMESTONE_ALLOCATE_COLOR) as assignment:
        print(assignment.registers, assignment.spilled)

with limestone.UniselDocument.from_file("graph-with-target.umd") as document:
    model = document.analyze()
with model:
    print(model.candidates, model.clauses, model.text)
    selection = model.select(algorithm=limestone.LIMESTONE_SELECT_GLOBAL)
with selection:
    print(selection.cost, selection.matches, selection.text)

with limestone.Optimizer("(operator add 2)(rule zero (add ?x 0) ?x)") as optimizer:
    optimizer.set_cost("add", 3)
    optimizer.set_limits(nodes=1000, classes=1000)
    optimized = optimizer.saturate("(add 42 0)")
with optimized:
    print(optimized.expression, optimized.info, optimized.trace)
```

Owning convenience handles support context managers and idempotent `close()`;
closed access and copying are rejected. Text/byte properties return Python-owned
copies. Checked convenience operations raise `LimestoneError` carrying the C
status code and message.

`compile_umd_file(path)` and `BURSDocument.from_file(path)` load relative includes
through the bounded, cycle-checked C file APIs. Text constructors take source
text and do not implicitly access the filesystem for includes.
`BURSAnalysis` owns its state/attempt data independently of the document, and
inspection remains available when a well-formed tree has no legal derivation.
`UniselDocument.analyze()` returns an owning `SelectionModel` independent of the
document; candidates include covered/input/output identities and provenance.
Signed clauses use 1-based candidate indices. Uncovered graphs have inspectable
empty clauses; solving them raises `LimestoneError` with `LIMESTONE_UNSATISFIABLE`.
`Selection` owns matches in emission order and its canonical Schedrow handoff.

`BinaryArchitecture(isa_source)`, `BinaryRuntime(source, target=None,
hot_threshold=10, max_regions=1024)`, and `TranslatedRegion` provide owning
translation/observation handles. Runtime architectures are copied. `prepare(data,
guest_address, target_address=0)` returns a region with Python-owned `bytes` and
`guest_bytes`, plus `valid` and `compiled` properties. `invalidate(address, size)`
returns the number of invalidated published views. Retained regions remain
inspectable after runtime destruction. The convenience runtime has no installer;
native installation uses the raw C callback ABI supplied by an extension.

`Optimizer` owns a Tunah session, costs and budgets. `load_rules`,
`load_rules_file`, `define_operator`, `set_cost`, and `set_limits` use the checked
C API. `define_graph_operator(opcode, term_operator, type, commutative=False)`
declares a typed pure opcode; `attach(raw_target_handle)` snapshots the optimizer
into a pipeline target independently of the source handle. Host analyses and
cancellation use the raw C callbacks from an extension. `Optimization` results
retain expressions, statistics and traces after their optimizer closes.

`Optimizer.binary_transform(context_identity, legality, userdata=None,
release=None)` snapshots a semantic optimizer using a native extension's C proof
callback. `BinaryTransform` owns it and exposes `identity` and `cacheable`.
`BinaryArchitecture.translate(data, target=None, source_address=0,
target_address=0, transform=None)` returns copied bytes;
`disassemble(data, address=0)` returns a copied listing. `BinaryRuntime(...,
transform=transform)` copies the same adapter for observation-time optimization.
The host extension must stay loaded while any callback snapshot is retained.

`ObjectTarget(isa_source)` owns ELF identity and relocation contracts. `create()`
returns a mutable builder; `from_code(data, symbol)` and `from_module(module,
symbol)` package raw/encoded bytes. `ObjectFile(data, source_name=None)` loads an
owning ELF32/ELF64 snapshot. Builders provide `add_section`, `add_symbol` and
`add_relocation`; `emit()`, `sections`, `symbols`, and `relocations` return owning
Python copies. `elf_class` reports 32 or 64. Use
`add_relocation(..., implicit_addend=True)` for REL records whose addends are
already encoded in section bytes; otherwise the builder creates RELA records.
`target.link(objects, base_address=0, max_size=64*1024*1024, externals={})`
returns an independent `LinkedImage` with copied `bytes`, `symbols`, and
`base_address`. Input handles may close after linking. Installation/execution
remains the host runtime's responsibility. See `bin2bin/OBJECTS.md`.

The complete raw `limestone_*` functions are also exposed. These retain C
ownership: destroy every returned raw handle exactly once. Output scalar pointers
are returned as additional Python tuple fields; `UInt32Array` and `ByteArray`
provide storage for raw input spans. Keep arrays alive through each synchronous
call. Do not manually destroy a pointer managed by a convenience handle.
Output temporaries are initialized to zero when a checked raw call fails; the
status determines whether its outputs are meaningful. Memory descriptors own
assigned address-space strings, and `set_alias_sets([17, 23])` copies and retains
alias-array storage. Borrowed output string fields are read-only. The installed
consumer test exercises import and execution after relocating the installation.
