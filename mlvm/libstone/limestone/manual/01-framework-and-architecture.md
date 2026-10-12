# Chapter 1. Framework and Architecture

[Manual contents](README.md) · [Next: Building and installing](02-building-installing-and-validating.md)

## 1.1 What Limestone provides

Limestone occupies the late stages of a compiler. Its central input is an explicit
machine-independent computation graph, accompanied by target information. Its
central output is a machine-oriented representation, optionally allocated,
encoded, and packaged into an object file. Around that compiler path are reusable
facilities for binary translation, equality saturation, metatracing, native
interoperability, and virtual-machine skeleton generation.

The framework is useful at several scales. An application can invoke the entire
pipeline through `run_pipeline`, embed only the register allocator, load a BURS
rule document to investigate instruction choices, or use Bin2Bin to translate
already encoded instructions. These uses share contracts and metadata while
retaining independent algorithms. A scheduler does not need to understand a SAT
solver, and a binary object loader does not need to run instruction selection.

Limestone's most important organizing principle is that **a fact has an owner**.
Metacode owns target-description normalization. Selection owns instruction
choices. Schedrow owns scheduling legality and resource assignments. RegTL owns
allocation problems and physical assignments. A backend owns encoding and ABI
lowering. The orchestration library connects these facilities without becoming a
second implementation of them.

## 1.2 The connected compiler pipeline

The normal compiler path is:

```text
machine-independent graph + explicit target description
                       |
                  preparation
                       |
              optional optimization
                       |
             Unisel or Limeburg
                       |
                selected region
                       |
              optional scheduling
                       |
               optional allocation
                       |
       spill materialization / physical hazards
                       |
          final scheduling and emission order
                       |
          MachineIR listing and region exchange
                       |
              optional backend encoding
                       |
              optional ELF object packaging
```

Preparation establishes structural and effect invariants. Optimization consumes
and returns a graph under a declared semantic vocabulary. Selection produces
target operations with virtual value identities. Scheduling assigns issue cycles,
slots, and resources. Allocation assigns storage, possibly requesting spills.
Materialization introduces the target's declared reload/store operations and
private frame. Final scheduling accounts for physical storage reuse and inserted
transfers. Encoding consumes this final representation.

The sequence matters. Two SSA values can be independent before allocation and
still share the same physical register at different points. Their storage reuse
creates final-order hazards. Likewise, a spill decision has no executable effect
until a target-specific load/store sequence implements it. Encoding an earlier
snapshot would omit both kinds of information.

## 1.3 Components and their responsibilities

| Component | Primary responsibility | Important boundary |
| --- | --- | --- |
| Metacode | Parse and normalize ISA metadata | Target properties are explicit input |
| Unisel | Minimum-cost graph instruction selection | Satie remains behind the solver adapter |
| Limeburg | Typed BURS/tree dynamic programming | Sharing policy is explicit |
| Schedrow | Dependency/resource scheduling and verification | Semantic edges remain distinct from scheduler-only edges |
| RegTL | Liveness, interference, allocation, and transfers | Physical assignment is separate from selection |
| Tunah | Bounded equality saturation and checked IL adapters | Equivalences and analyses come from the host |
| MachineIR | Architecture-neutral machine IR in D | Target metadata remains a separate machine description |
| Bin2Bin | Decode, lift, translate, encode, and observe binary regions | Execution/state models and codec support are explicit |
| TraceML | Functional frontend and MetaKrivine execution | Lazy source semantics precede target lowering |
| Exolayer | Host C ABI/FFI and extension boundary | Host ABI is not inferred from a compiler target |
| VMWeave | Lua/C++ specification compiler, STK-00 and optional C runtimes | Native semantic lowering and runtime policies use explicit adapters |
| Limestone core | Coordinate stages and expose embedding entry points | Algorithms stay in their owning components |

The repository directories use these names directly. Public C++ headers are
located beside the implementation, for example `schedrow/schedrow.hpp` and
`bin2bin/object.hpp`. Public C handles for independent IL workflows are grouped
in `limestone/il.h`. This arrangement is also reflected by the installed include
layout.

## 1.4 Representations are contracts

A representation specifies more than a convenient container shape. It states
what identities exist, how values flow, what effects are observable, and what
the next stage can rely on.

The Unisel graph uses node IDs for source computations and produced SSA values.
A candidate identifies covered computations separately from boundary inputs and
outputs. Scheduler IR uses instruction IDs and value IDs in separate roles.
Architectural register effects occupy another domain. RegTL consumes lifetimes,
classes, aliases, clobbers, and fixed/tied constraints. MachineIR exchange retains
these distinctions inside an owning envelope.

Consider an instruction that produces a virtual value and also writes a status
register. A value ID of `7` and an architectural register ID of `7` do not denote
the same object. Their result-latency maps are distinct. Treating their equal
numeric spelling as identity would introduce incorrect dependencies or lose
the status effect.

Similarly, an immediate value consumed inside a pattern is different from a
register value whose contents happen to be known. When a shared constant crosses
a BURS forest boundary, it retains its register identity. Knowledge of its value
can prove predicates, but does not automatically turn every use into an encoded
immediate.

## 1.5 ISA inventories and executable support

The Infobank describes many architectures and instructions. Several levels of
capability must be considered when using an entry:

1. The description can be parsed and its metadata retained.
2. An instruction has a semantically justified source selection pattern.
3. The target provides timing and resource information needed for scheduling.
4. Values have legal register classes and allocation constraints.
5. A supported codec or backend can encode the selected operands.
6. ABI and runtime contracts make the emitted program callable or interpretable.

Support at one level does not imply support at the next. A mnemonic and a short
semantic expression can be useful inventory information without establishing a
generic compiler equivalence. A decoded instruction may carry an unsupported
translation status. An ELF machine number identifies a container contract; it
does not provide a calling convention.

The generated Limeburg specifications are a practical illustration. All manifest
architectures have specification files, but inventory-derived rules use qualified
`ISA_*` roots. Generic source computations are declared through explicit
`selection_tree` contracts. Coverage records explain instructions that need
additional operands, type information, semantics, or adapters.

## 1.6 Semantics and costs answer different questions

Semantic legality determines whether an implementation is allowed. Cost chooses
among allowed implementations. A lower numerical cost cannot justify removing a
store, replacing checked arithmetic with wrapping arithmetic, changing a branch
target, or selecting an unencodable immediate.

Limestone consequently keeps several independent numerical models:

- pattern/rule costs used by instruction selection;
- local operator/literal costs used by Tunah extraction;
- result latencies and resource reservations used by scheduling;
- allocation constraints and lifetime information used by RegTL;
- encoded widths and field ranges used by the backend.

These models can be coordinated by a host target adapter. They are not aliases
for one universal performance number. Throughput metadata, for example, is
preserved by the scheduler, while hard issue-rate limits are expressed through
issue width, slots, and reservations.

## 1.7 Control flow and effects

The source SSA graph is acyclic. The CFG may contain loops. These are compatible
properties because value edges and control-flow edges are separate relations.
Cross-block definitions must dominate their uses; phi elimination and boundary
transfers belong to explicit lowering adapters.

Blocks are declared in final layout order, with entry first. Conditional target
index `1` is fallthrough. Scheduling is block-local unless an adapter constructs
a legal broader region. The issue vector must still respect the declared layout,
even when cycles restart at zero in each block.

Memory effects include read/write, volatility, atomicity, ordering, address
spaces, alias sets, size, and alignment. Calls, traps, barriers, and terminators
also constrain movement. A target model may add restrictions, but cannot erase
an effect required by the source. These contracts survive selection, allocation,
serialization, and encoding.

## 1.8 Ownership and inspectability

C++ results use owning values and RAII-managed state. The C APIs expose opaque
handles with matching destroy operations. Many handles are independent snapshots:
a selection model can outlive its document, a result can outlive its model, and
a compiled module can outlive its program and target.

Borrowed output views have a different lifetime. A C string returned from a
module is valid until that module is destroyed. Object inspection views may also
be invalidated by mutation. Python convenience properties copy text and bytes
into Python-owned objects, while raw bindings preserve C ownership rules.

Inspection is intentionally available before and after algorithms run. Unisel
exposes candidates and clauses. Limeburg exposes states and rejection attempts.
Schedrow exposes dependencies and issue assignments. RegTL retains original spill
decisions alongside final transfers and frame information. Runtime regions retain
immutable bytes even after validity expires.

This makes it possible to diagnose the first incorrect boundary instead of
guessing from final machine code.

## 1.9 Terminology to carry through the manual

| Term | Meaning |
| --- | --- |
| Source graph | Machine-independent computations, SSA values, effects, and CFG |
| Target | Explicit instruction patterns and downstream machine contracts |
| Pattern | A computation a target instruction can implement |
| Candidate | One legal pattern match at particular source identities |
| Cover | A compatible set of candidates implementing required computations |
| Nonterminal | A BURS derivation category with a stable identity |
| Selected region | Target operations before final storage/encoding decisions |
| Schedule | Issue cycles, optional slots, and chosen resources |
| Allocation | Register assignments and explicit spill decisions |
| Materialization | Concrete implementation of spills/transfers |
| Exchange envelope | Owning serialized program plus machine-facing contracts |
| Lifted instruction | Decoded semantics with source address and control boundary |
| Addressed image | Linked bytes with an explicit installation base |
| Installer | Host-owned conversion from translated bytes to an owning callable |

## 1.10 Navigating the implementation

Start with [the pipeline header](../limestone/limestone.hpp) and
[implementation status](../IMPLEMENTATION.md) for a whole-framework view. Consult
the public component header for exact types and the grammar under `parsers/` for
text syntax. The fixtures under `tests/fixtures/` provide small explicit target
models. Component tests demonstrate both successful behavior and rejected input.

The remaining chapters explain these contracts in detail, with worked examples
that progress from closed integer evaluation to selected graphs, encoded regions,
objects, and embedding runtimes.

[Manual contents](README.md) · [Next: Building and installing](02-building-installing-and-validating.md)
