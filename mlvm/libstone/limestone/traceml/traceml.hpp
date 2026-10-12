#pragma once
#include "../limestone/foundation.hpp"
#include "../unisel/unisel.hpp"
#include <map>
namespace limestone::traceml {
struct Expr {
  enum class Kind { Symbol, Integer, Apply, Lambda, If, Begin };
  Kind kind; std::string atom; int64_t integer=0; std::vector<std::shared_ptr<Expr>> children;
  size_t offset=0; uint32_t line=1, column=1;
};
Result<std::shared_ptr<Expr>> parse(std::string_view);
struct Trace { uint32_t id; std::shared_ptr<Expr> guard, body; };
struct Program { std::vector<std::shared_ptr<Expr>> forms; };
Result<Program> compile(std::string_view);
Result<int> verify(const Program&);
// Call-by-name lexical closures; integer primitives are strict, left-to-right.
Result<int64_t> evaluate(const Program&, size_t step_limit=100000);
struct TraceEvent {
  enum class Kind { Apply, Bind, Lookup, Integer, Primitive, Branch, Sequence, Return } kind;
  size_t step=0, offset=0;uint32_t line=1,column=1;
  std::string operation;
  std::optional<uint32_t> value;
  std::vector<uint32_t> inputs;
  std::optional<int64_t> result;
  std::optional<bool> taken;
  bool operator==(const TraceEvent&)const=default;
};
struct ExecutionOptions {
  size_t step_limit=100000, event_limit=100000;
  bool record_trace=true;
  std::function<bool()> cancelled;
  // Synchronous observer; events are borrowed and callbacks may not mutate IR.
  std::function<void(const TraceEvent&)> observer;
  bool record_guards=false;
};
struct ExecutionResult {
  int64_t value=0;size_t steps=0;
  std::optional<uint32_t> result_value;
  std::vector<TraceEvent> trace;
};
// The same MetaKrivine evaluator serves ordinary and traced execution.
Result<ExecutionResult> execute(const Program&,const ExecutionOptions& = {});
std::string print_trace(const ExecutionResult&);
// Structured lowering errors are available through this checked entry point.
Result<std::string> lower_checked(const Program&);
std::string lower_to_machineir(const Program&);
// Portable constant/return adapter used by the orchestration convenience API.
struct PortableLowering {
  unisel::Program program;
  std::vector<unisel::Pattern> patterns;
  std::vector<schedrow::Instruction> instruction_models;
};
PortableLowering lower_graph(int64_t value);
// Closed recorded execution -> checked arithmetic and guard pseudo-operations.
// Guard failures require a backend deoptimization implementation before encoding.
Result<PortableLowering> lower_trace(const ExecutionResult&);

struct RuntimeValueStorage;
struct RuntimeProgramStorage;
struct GuardState;
struct RuntimeAccess;
// Immutable owning runtime values. Function values retain lexical environments
// and lazy argument closures; source ASTs may be destroyed or edited afterwards.
class RuntimeValue {
  std::shared_ptr<const RuntimeValueStorage> storage_;
  explicit RuntimeValue(std::shared_ptr<const RuntimeValueStorage> storage):storage_(std::move(storage)){}
  friend struct RuntimeAccess;
public:
  RuntimeValue()=default;
  static RuntimeValue integer(int64_t);
  std::optional<int64_t> integer() const;
  bool callable() const;
};
class RuntimeProgram {
  std::shared_ptr<const RuntimeProgramStorage> storage_;
  explicit RuntimeProgram(std::shared_ptr<const RuntimeProgramStorage> storage):storage_(std::move(storage)){}
  friend struct RuntimeAccess;
public:
  RuntimeProgram()=default;
  bool valid() const {return bool(storage_);}
};
// Safepoint after evaluating an if condition. The snapshot owns the lexical
// environment, pending applications, strict primitive/sequence continuations,
// and following top-level forms. No native stack addresses are retained.
class GuardSnapshot {
  std::shared_ptr<const GuardState> storage_;
  explicit GuardSnapshot(std::shared_ptr<const GuardState> storage):storage_(std::move(storage)){}
  friend struct RuntimeAccess;
public:
  GuardSnapshot()=default;
  bool valid() const {return bool(storage_);}
  bool expected() const;
  size_t offset() const;
};
struct RuntimeResult {
  RuntimeValue value;
  ExecutionResult execution;
  std::vector<GuardSnapshot> guards;
};
Result<RuntimeProgram> lower_runtime(const Program&,size_t node_limit=100000);
// Runtime arguments are applied to the final form. Application stays lazy and
// lexical; earlier forms are evaluated in declaration order as in execute().
Result<RuntimeResult> run_runtime(const RuntimeProgram&,std::span<const RuntimeValue> arguments={},const ExecutionOptions& = {});
Result<RuntimeResult> apply_runtime(const RuntimeValue&,std::span<const RuntimeValue>,const ExecutionOptions& = {});
// The backend supplies the actual condition at the safepoint. Restoration is
// synchronous and transactional; callbacks/observers follow execute()'s rules.
Result<RuntimeResult> resume_guard(const GuardSnapshot&,int64_t condition,const ExecutionOptions& = {});
struct GuardValueSlot {
  enum class Kind { Integer, Callable, Delayed } kind=Kind::Delayed;
  uint32_t id=0;
  std::string path;
  std::optional<int64_t> integer;
};
// Stable declaration-order recovery slots cover lexical bindings, pending lazy
// arguments and strict primitive continuation values. Delayed expressions are
// inspected without evaluating them. Replacements own their values/environments.
Result<std::vector<GuardValueSlot>> guard_value_slots(const GuardSnapshot&,size_t work_limit=1000000);
Result<GuardSnapshot> recover_guard_values(const GuardSnapshot&,const std::map<uint32_t,RuntimeValue>&,size_t work_limit=1000000);
}
