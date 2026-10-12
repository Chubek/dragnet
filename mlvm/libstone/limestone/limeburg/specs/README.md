# Infobank-derived Limeburg specifications

The 23 `.lburg` files in this directory are generated from the architectures in
[`metacode/infobank/manifest.json`](../../metacode/infobank/manifest.json).
They use Limestone's existing Limeburg grammar and are directly loadable by
`load_rules_file`, the C/Python BURS document APIs, and the CLI.

## Generation and validation

From the repository root:

```sh
cmake -S . -B build
cmake --build build --target limeburg-regenerate-specs
build/limeburg-generate-specs --check metacode/infobank limeburg/specs
ctest --test-dir build -R limeburg-specs --output-on-failure
```

Edit the Infobank sources and regenerate. The generator uses Metacode's ISA
parser, SExprTk, normalized Unisel machine descriptions, and the existing
UMD-to-BURS adapter. Each output passes the actual GLR rule loader and canonical
serialization round trip before writing. `--check` is read-only and fails on
stale or missing outputs. Architecture identities, instruction/semantic counts,
and register classes are checked against the manifest. The three legacy marker
files are identified by the manifest as markers rather than architectures.

The C++ generation API is `limeburg/infobank.hpp`, available through
`Limestone::limeburg_infobank`. `InfobankSpec::machine` owns the normalized
machine metadata; `document` contains the rules, and `coverage` explains each
instruction's disposition. The executable and this directory are installed with
Limestone, under `bin` and `share/limestone/limeburg/specs` respectively.

## Selection contract

The Infobank is an instruction inventory. Many short `instruction_selection.pattern`
hints and semantic expressions intentionally omit types, extension availability,
or architectural state. The generated rules preserve instruction identity:

- An explicit `tooling.instruction_selection.selection_tree` is imported as a
  declarative source computation, retaining types, costs, classes, and predicates.
- Inventory-derived rules use an instruction-scoped `ISA_<instruction>` root.
  Its children preserve the semantic expression's operands and nested structure.
  The root implicitly identifies the original semantic operator and ISA context.
  These roots represent already-qualified target intrinsics: the source adapter
  establishes their instruction availability and semantic meaning.
- Nested expressions use `SEM_<operator>_<arity>` terminals. Literal symbolic
  and string tags use distinct `TAG_symbol_*` and `TAG_string_*` leaves. Names
  escape non-alphanumeric bytes as `_hh`, including `_` as `_5f`, so spelling
  cannot cause collisions. For example, `i32_add` becomes `ISA_i32_5fadd`.
- `value` is a single register/SSA result; its root and register inputs carry
  explicit register-class constraints. `INPUT()` derives an external `value`
  only when `required false` and the matching register class are supplied.
- `stmt` represents operations without an explicit register result. Stack-machine
  operations remain statements with observable stack state; their source adapter
  supplies ordering and state dependencies.
- `CONST()` holds a signed-64-bit source immediate. Operand immediates require
  explicit encoding bit slices and signedness. Split fields retain logical width
  and explicit low-bit alignment, checked through `multiple_of` predicates.
  Literal integer semantics use exact-value constraints.

Rule IDs are one-based instruction positions in the source inventory; unsupported
instructions leave gaps. External-value rule IDs start at 65,536. Inventory rules
use an explicit **instruction-count** cost of one, with zero-cost external
values; a declared selection cost overrides the inventory cost. Scheduling
latency, throughput, and resource placeholders retain their original values.

Each file records architecture/family/model/version, source-located rule origins,
original semantics and operands, encoding references, dataflow, allocation,
effect, and scheduling metadata. Source/target adapters apply those contracts to
their IR: memory access size/order/aliasing, calls and CFG edges, flags, implicit
state, result/input ties, fixed registers, scheduling, and encoding remain
explicit downstream responsibilities. `coverage.json` supplies the required
inventory source-node properties alongside the rule IDs.

### Example

[`examples/riscv64-addi.limeburg`](examples/riscv64-addi.limeburg) includes the
RISC-V specification and selects `addi` with an immediate of seven:

```sh
build/limestone-cli --select-burs limeburg/specs/examples/riscv64-addi.limeburg
build/limestone-cli --analyze-burs limeburg/specs/examples/riscv64-addi.limeburg
```

Its register input is external, the constant is covered by the instruction, and
the reported cost is one. Out-of-range immediates and wrong register classes
have no valid derivation.

## Coverage

The current manifest contains **3,395 instructions**. These specifications have
**1,935 instruction rules** and **1,460 recorded coverage boundaries**.
Unsupported operations have an explicit reason in both their file and
`coverage.json`. Common boundaries include absent immediate signedness, multiple
immediates without per-operand contracts, contradictory semantic/dataflow reads,
memory operands requiring an addressing adapter, multiple results, and SPIR-V
type/result IDs requiring typed virtual-ISA lowering. These records identify
where explicit selection trees or richer target adapters are needed.

| Specification | Inventory | Instruction rules | Boundaries |
| --- | ---: | ---: | ---: |
| `nvptx.lburg` | 42 | 36 | 6 |
| `x86.lburg` | 144 | 63 | 81 |
| `mips32.lburg` | 62 | 57 | 5 |
| `s390x.lburg` | 70 | 65 | 5 |
| `ppc32.lburg` | 64 | 57 | 7 |
| `m68k.lburg` | 104 | 31 | 73 |
| `aarch64.lburg` | 315 | 169 | 146 |
| `sparc64.lburg` | 107 | 107 | 0 |
| `amdgpu.lburg` | 139 | 139 | 0 |
| `riscv64.lburg` | 252 | 219 | 33 |
| `spirv.lburg` | 873 | 22 | 851 |
| `loongarch64.lburg` | 148 | 133 | 15 |
| `loongarch32.lburg` | 91 | 80 | 11 |
| `mips64.lburg` | 84 | 71 | 13 |
| `tricore.lburg` | 31 | 23 | 8 |
| `arm32.lburg` | 114 | 93 | 21 |
| `directx.lburg` | 34 | 31 | 3 |
| `avr.lburg` | 68 | 47 | 21 |
| `ppc64.lburg` | 90 | 82 | 8 |
| `wasm.lburg` | 200 | 148 | 52 |
| `riscv32.lburg` | 199 | 171 | 28 |
| `bpf.lburg` | 138 | 65 | 73 |
| `hexagon.lburg` | 26 | 26 | 0 |
