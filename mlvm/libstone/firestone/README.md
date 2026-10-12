# Firestone

Firestone is a dependency-free, header-only C++17 library for compiler IRs,
graph analysis, CPS, type systems, semantic checking, memory management,
object models, term rewriting, equality saturation, bytecode virtual machines,
arbitrary-precision arithmetic, concurrency, portable vector operations, and
compute/graphics adapters.

## Build and test

Requires CMake 3.20+ and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/firestone
```

Use `-DBUILD_TESTING=OFF` for an installation-only configuration. Tests cover
SSA diamonds and loops, dominance failures, phi-edge liveness, dependence
slicing, memory merges, regional signatures, rewriting limits, congruence
closure, cyclic extraction, CPS lowering, type substitution, object dispatch,
allocation/collection, checked arithmetic, stack/register execution, recursive calls,
phi-correct SSA compilation, malformed bytecode, tiering contracts, big-number
arithmetic/rounding, channel shutdown and scheduling, vector overflow semantics,
compute/display resource lifetimes, SPIR-V framing, cross-component hardening
contracts, and relocated installed-package consumers.
Every public header is also compiled independently in C++17 mode.

## Embedding

```cmake
find_package(Firestone 0.1 CONFIG REQUIRED COMPONENTS ir rewrite eqsat)
target_link_libraries(my_compiler PRIVATE Firestone::firestone)
```

Individual targets are `Firestone::ir`, `Firestone::rewrite`, `Firestone::eqsat`,
`Firestone::cps`, `Firestone::memory`, `Firestone::object`, `Firestone::sema`, and
`Firestone::type`, `Firestone::vm`, `Firestone::bignum`, `Firestone::compute`,
`Firestone::concurrency`, `Firestone::gfx`, and `Firestone::intrin`.
Source-tree embedding uses `add_subdirectory(firestone)`
with the same target names. Include `<Firestone.hpp>` or individual headers,
such as `<libir/SimpleSSAGraph.hpp>` and `<libeqsat/Saturation.hpp>`.

## Components

| Header / API | Implemented behavior |
| --- | --- |
| `libir/Graph.hpp` | Stable node IDs, typed/ported edges, reachability, deterministic topological ordering, SCCs, DOT output |
| `RawDAG.hpp` | Cycle-rejecting DAG construction |
| `ControlFlowGraph.hpp` | Basic blocks, terminators, dominance, postdominance, frontiers, control dependence |
| `Untyped3AC.hpp`, `Typed3AC.hpp` | Explicit registers and linear instructions; arity, use-order, target and type checks |
| `Triplets.hpp`, `Quadruplets.hpp` | Implicit positional results and explicit destinations with at most two operands |
| `SimpleSSAGraph.hpp` | Typed single definitions, parameters, reserved loop values, predecessor-labelled phis, dominance verification |
| `GatedSSAGraph.hpp` | Boolean predicates on incoming phi edges |
| `MemorySSAGraph.hpp` | Conservative memory version graph, load uses, store/call definitions, memory phis, reaching definitions |
| `DataflowGraph.hpp` | Typed, pure, acyclic operations and scheduling |
| `DataDependenceGraph.hpp` | Instruction/parameter nodes, def-use edges, forward/backward slices |
| `ValueDependenceGraph.hpp` | Value-only dependencies, including loop-phi cycles |
| `ProgramDependenceGraph.hpp` | Data dependence plus CFG-derived control dependence |
| `InterferenceGraph.hpp` | Phi-aware fixed-point liveness, interference, deterministic greedy coloring with precolors |
| `CallGraph.hpp` | Named function declarations, call sites, recursive SCC discovery |
| `ANFGraph.hpp` | Ordered, typed single-assignment let-bindings and a final atomic result |
| `CPSGraph.hpp` | Continuation parameters, local bindings/effects, tail transfers and signature/scope checks |
| `ClickSeaOfNodesGraph.hpp` | Explicit value/control inputs, branches and control projections, regions, cyclic phis, effects, exits |
| `RVSDG.hpp` | Typed ports, isolated regions, simple/stateful nodes, gamma, theta and lambda signatures |
| `librewrite/Term.hpp`, `Rewrite.hpp` | S-expression terms, repeated-variable matching, substitution, guarded bottom-up rewriting |
| `libeqsat/EGraph.hpp`, `Saturation.hpp` | Hash-consing, union-find, congruence rebuilding, e-matching, bounded saturation, minimum-cost extraction |
| `libir/DominatorTree.hpp`, `LoopAnalysis.hpp` | Dominator forests, nearest common dominators, natural loops, nesting, SCC-based irreducible regions |
| `libir/DataflowAnalysis.hpp`, `PassManager.hpp` | Bounded forward/backward worklists and verified, transactional SSA pass pipelines |
| `libcps/Continuation.hpp`, `CPSBuilder.hpp` | Continuation signatures, scoped transfer validation, named construction |
| `libcps/CPSAnalysis.hpp`, `CPSLowering.hpp` | Recursive demand/liveness analysis and phi-correct CPS-to-SSA lowering with tail-edge splitting |
| `libmemory/Alloc.hpp`, `Pool.hpp`, `GC.hpp` | Move-only aligned storage, fixed-block allocation, explicit tracing mark/sweep with stable handles |
| `libmemory/Arena.hpp`, `Buffer.hpp`, `IntrusivePtr.hpp`, `Resource.hpp` | Monotonic allocation, owning byte buffers, atomic intrusive reference counting, PMR adapters |
| `libobject/ObjectGraph.hpp`, `VTable.hpp` | Named fields/references, cyclic object graphs, stable virtual slots and checked overrides |
| `libobject/ClassHierarchy.hpp`, `ObjectLayout.hpp`, `Dispatch.hpp`, `ObjectVisitor.hpp` | Single inheritance, deterministic layouts, checked dynamic dispatch, cycle-preserving deep cloning |
| `libsema/TypeCheck.hpp`, `BlockCheck.hpp`, `SymCheck.hpp`, `OverflowCheck.hpp` | Representation/type checks, return signatures, symbol resolution, UB-free signed arithmetic checks |
| `libsema/SymbolTable.hpp`, `CallCheck.hpp`, `DefiniteAssignment.hpp`, `EffectCheck.hpp` | Lexical scopes, callee signatures, must-assignment dataflow, configurable effect summaries |
| `libtype/Atomic.hpp`, `Imperative.hpp`, `SystemF.hpp` | Shared scalar types, structural imperative types, polymorphic lambda terms and explicit System F type checking |
| `libtype/TypeLayout.hpp`, `TypePrinter.hpp`, `Unification.hpp`, `Conversion.hpp` | Checked target-configurable layout, formatting, occurs-checked type unification, conversion classification |
| `librewrite/Traversal.hpp`, `Unification.hpp`, `CriticalPairs.hpp`, `Strategy.hpp` | Positioned traversal, first-order unification, symbolic rule overlaps, composable rewriting strategies |
| `libeqsat/Analysis.hpp`, `CostModel.hpp`, `Extraction.hpp`, `Visualization.hpp` | Bounded semilattice analysis, operator costs, shared extracted DAGs, statistics and DOT rendering |
| `libvm/Module.hpp`, `Opcode.hpp`, `CodeTape.hpp` | Shared bytecode metadata, signatures, control/arity checks, stack-height and definite-assignment verification |
| `libvm/StackVM.hpp`, `RegisterVM.hpp`, `Environment.hpp` | Checked scalar execution, mixed-model calls, native bindings, globals, heap cells, schema-checked objects, resumable budgets |
| `libvm/Compiler.hpp`, `BytecodeBinary.hpp`, `Loader.hpp` | Transactional SSA lowering with parallel phi copies, bounded portable binary decoding, linking, caching and disassembly |
| `libvm/*ThreadedCode.hpp`, `MethodJIT.hpp`, `TraceJIT.hpp`, `AOT.hpp`, `OSR.hpp`, `Deopt.hpp` | Portable host-handler dispatch, method hotness, trace recording/guards, backend callbacks, explicit activation transfer and reconstruction |
| `libbignum/BigInt.hpp`, `BigFloat.hpp`, `Complex128.hpp` | Signed arbitrary-precision integers; finite decimal floating point with configurable precision and ties-to-even rounding; two-component binary64 complex numbers |
| `libcompute/CUDA.hpp`, `HIP.hpp`, `OpenCL.hpp`, `SYCL.hpp` | SDK callback adapters, shared context/resource ownership, checked transfers, kernel compilation/dispatch contracts |
| `libcompute/OpenMP.hpp` | Host parallel iteration, deterministic map/fold, exception propagation, serial fallback |
| `libgfx/GLSL.hpp`, `HLSL.hpp`, `MSL.hpp`, `WGSL.hpp`, `SPIR-V.hpp` | Shader-source contracts, compiler callback, SPIR-V binary framing/serialization and ID allocation |
| `libgfx/Wayland.hpp`, `X11.hpp`, `DRM.hpp` | Native display callback adapters, move-only surfaces, checked frames and event polling |
| `libintrin/SSE4.hpp`, `AVX2.hpp`, `AVX512.hpp`, `NEON.hpp`, `FPU.hpp` | Portable vector lane operations/masks, defined modular integer arithmetic, feature queries, scalar FP environment helpers |
| `libconcurrency/*.hpp` | Atomics/locks, counting semaphore, closeable MPMC channels, joining native threads, stackless fibers and round-robin scheduler, Linux process/affinity wrappers |

Namespaces follow component names: `firestone::ir`, `firestone::rewrite`,
`firestone::eqsat`, `firestone::cps`, `firestone::memory`, `firestone::object`,
`firestone::sema`, `firestone::type`, `firestone::vm`, `firestone::bignum`,
`firestone::compute`, `firestone::gfx`, `firestone::intrin`, and `firestone::concurrency`.
The original `DataDependenceGrahp.hpp` spelling remains a compatibility include
and type alias.

## Example: equality saturation

```cpp
#include <libeqsat/Saturation.hpp>

using firestone::rewrite::parse;
firestone::eqsat::EGraph graph;
auto root = graph.add(parse("(add (mul x 1) 0)"));
firestone::eqsat::Saturator runner;
runner.add_rule({"mul-one", parse("(mul ?x 1)"), parse("?x")});
runner.add_rule({"add-zero", parse("(add ?x 0)"), parse("?x")});
auto status = runner.run(graph);
auto best = graph.extract(root); // term: x, default cost: 1
```

Rules describe caller-supplied equalities over opaque operators. Type, overflow,
floating-point, and effect semantics must be encoded in operator names or guards.
The engine installs no implicit algebraic rules.

## Example: typed continuations to SSA

```cpp
#include <libcps/CPSBuilder.hpp>
#include <libcps/CPSLowering.hpp>

using namespace firestone::ir;
firestone::cps::CPSBuilder builder;
auto entry = builder.create("entry", {Type::integer()});
auto finish = builder.create("finish", {Type::integer()});
auto value = builder.graph().bind(entry, Opcode::Add, Type::integer(),
    {builder.parameter(entry, 0), Operand::constant(1)});
builder.jump(entry, finish, {Operand::ref(value)});
builder.graph().halt(finish, builder.parameter(finish, 0));
auto lowered = firestone::cps::lower_to_ssa(builder.finish());
auto errors = lowered.ssa.verify(); // empty
```

See [the API guide](docs/API.md) for construction contracts and
[the installation guide](INSTALL.md) for integration and sanitizer commands.
The [VM guide](docs/VM.md) specifies bytecode, execution, binary, and backend contracts.
The [foundation libraries guide](docs/Foundations.md) specifies arithmetic,
concurrency, vector semantics, and compute/graphics adapter contracts.
