#pragma once
#include "traceml.hpp"
#include <map>

namespace limestone::traceml {
struct NativeLocation {
  enum class Kind { Register, Stack, Constant } kind=Kind::Register;
  uint32_t reg=0;
  uint64_t offset=0;
  int64_t constant=0;
};
// Values/code owners are copied at the boundary. Stack bytes are an explicit
// safepoint image; no native stack addresses are retained or dereferenced.
struct NativeState {
  std::map<uint32_t,RuntimeValue> registers;
  std::vector<uint8_t> stack;
  std::string byte_order;
};
struct NativeGuard {
  uint32_t id=0;
  GuardSnapshot snapshot;
  NativeLocation condition;
  std::map<uint32_t,NativeLocation> values;
  size_t recovery_limit=1000000;
  // A target that changes lexical/continuation values supplies full recovery and
  // proof. Condition-only recovery may retain the current owning runtime snapshot.
  std::function<Result<GuardSnapshot>(const GuardSnapshot&,const NativeState&)> recover;
  std::function<Result<int>(const GuardSnapshot&,const GuardSnapshot&,const NativeState&)> prove;
};
Result<int64_t> recover_integer(const NativeState&,const NativeLocation&);
// Restores the MetaKrivine continuation, pending applications, lexical state and
// subsequent forms using the same runtime evaluator as ordinary execution.
Result<RuntimeResult> deoptimize(const NativeGuard&,const NativeState&,const ExecutionOptions& = {});
struct NativeArtifact {
  std::string machine_ir;
  std::vector<uint8_t> bytes;
  // Executable mapping/runtime/ABI owner, retained across active invocation.
  std::shared_ptr<const void> owner;
  std::function<Result<RuntimeResult>(std::span<const RuntimeValue>,const ExecutionOptions&)> invoke;
  std::vector<NativeGuard> guards;
};
struct NativeRuntimeAdapter {
  std::string identity;
  size_t byte_limit=64*1024*1024, guard_limit=100000;
  // Lower complete lexical call-by-name runtime semantics through a backend.
  // The adapter may use explicit runtime calls for closures/environments rather
  // than duplicate the evaluator. Compilation must not execute the source.
  std::function<Result<NativeArtifact>(const RuntimeProgram&)> lower;
  std::function<Result<int>(const RuntimeProgram&,const NativeArtifact&)> prove;
};
struct NativeProgramStorage;
// Opaque synchronous invocation frame. A backend emits a call/tail-call to the
// supplied dispatch entry with this frame unchanged. The runtime owns arguments,
// continuations and errors; no C++ exception crosses the native entry boundary.
struct RuntimeCallFrame;
using RuntimeCallEntry = void (*)(void*);
enum class RuntimeCallKind { Program, Closure, Deoptimization };
struct RuntimeCallCode {
  std::string machine_ir;
  std::vector<uint8_t> bytes;
  std::shared_ptr<const void> owner;
  RuntimeCallEntry entry=nullptr;
};
struct RuntimeCallBackend {
  std::string identity;
  size_t byte_limit=64*1024*1024;
  // The backend owns ABI lowering, instruction encoding and installation. The
  // entry calls dispatch exactly once and returns normally. Compilation never
  // evaluates the source. verify checks the emitted call and frame preservation.
  std::function<Result<RuntimeCallCode>(RuntimeCallKind,RuntimeCallEntry)> lower;
  std::function<Result<int>(RuntimeCallKind,RuntimeCallEntry,const RuntimeCallCode&)> verify;
};
class NativeProgram {
  std::shared_ptr<const NativeProgramStorage> storage_;
  explicit NativeProgram(std::shared_ptr<const NativeProgramStorage> storage):storage_(std::move(storage)){}
  friend Result<NativeProgram> lower_native(const RuntimeProgram&,const NativeRuntimeAdapter&);
  friend Result<RuntimeResult> run_native(const NativeProgram&,std::span<const RuntimeValue>,const ExecutionOptions&);
  friend Result<NativeProgram> lower_runtime_call(const RuntimeProgram&,const RuntimeCallBackend&);
  friend Result<NativeProgram> lower_runtime_call(const RuntimeValue&,const RuntimeCallBackend&);
  friend Result<NativeProgram> lower_runtime_call(const NativeGuard&,const RuntimeCallBackend&);
  friend Result<RuntimeResult> run_native_deoptimization(const NativeProgram&,const NativeState&,const ExecutionOptions&);
public:
  NativeProgram()=default;
  std::string_view identity() const;
  std::string_view machine_ir() const;
  std::span<const uint8_t> bytes() const;
};
// Native programs own their runtime source, executable code and callback state.
Result<NativeProgram> lower_native(const RuntimeProgram&,const NativeRuntimeAdapter&);
Result<RuntimeResult> run_native(const NativeProgram&,std::span<const RuntimeValue> arguments={},const ExecutionOptions& = {});
// Concrete runtime-call lowering supports the complete MetaKrivine language,
// owning callable closures and guard exits through one checked runtime bridge.
Result<NativeProgram> lower_runtime_call(const RuntimeProgram&,const RuntimeCallBackend&);
Result<NativeProgram> lower_runtime_call(const RuntimeValue&,const RuntimeCallBackend&);
Result<NativeProgram> lower_runtime_call(const NativeGuard&,const RuntimeCallBackend&);
Result<RuntimeResult> run_native_deoptimization(const NativeProgram&,const NativeState&,const ExecutionOptions& = {});
}
