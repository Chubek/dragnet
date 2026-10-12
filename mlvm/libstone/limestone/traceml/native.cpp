#include "native.hpp"
#include <bit>
#include <set>

namespace limestone::traceml {
struct NativeProgramStorage {
  RuntimeProgram program;std::string identity;NativeArtifact artifact;
  std::function<Result<RuntimeResult>(const NativeState&,const ExecutionOptions&)> deoptimize;
};
struct RuntimeCallFrame {
  std::function<Result<RuntimeResult>()> operation;
  std::optional<Result<RuntimeResult>> result;
  bool duplicate=false;
};
namespace {
template<class T,class Function> Result<T> checked(Function&& function){
  try{return function();}catch(const Error& error){return Result<T>::err(error);}catch(const std::bad_alloc&){return Result<T>::err({Error::Code::ResourceLimit,"TraceLambda native allocation failed"});}catch(const std::exception& error){return Result<T>::err({Error::Code::Internal,error.what()});}catch(...){return Result<T>::err({Error::Code::Internal,"TraceLambda native adapter exception"});}
}
Result<int> validate_guard(const NativeGuard& guard){
  if(!guard.snapshot.valid()||guard.condition.kind<NativeLocation::Kind::Register||guard.condition.kind>NativeLocation::Kind::Constant||bool(guard.recover||!guard.values.empty())!=bool(guard.prove))return Result<int>::err({Error::Code::InvalidArgument,"native guard needs a valid snapshot/location and complete recovery proof"});
  return Result<int>::ok(0);
}
void dispatch(void* input) noexcept {
  auto frame=static_cast<RuntimeCallFrame*>(input);
  if(!frame)return;
  if(frame->result){frame->duplicate=true;return;}
  frame->result=checked<RuntimeResult>(frame->operation);
}
Result<RuntimeCallCode> compile_call(RuntimeCallKind kind,const RuntimeCallBackend& backend){
  if(backend.identity.empty()||!backend.lower||!backend.verify)return Result<RuntimeCallCode>::err({Error::Code::InvalidArgument,"native runtime call needs backend identity, lowering and verification"});
  auto result=backend.lower(kind,dispatch);if(!result)return result;auto& code=result.value();
  if(!code.owner||!code.entry||code.bytes.empty()||code.machine_ir.empty())return Result<RuntimeCallCode>::err({Error::Code::Conflict,"native runtime call returned incomplete installed code/IR ownership"});
  if(code.bytes.size()>backend.byte_limit)return Result<RuntimeCallCode>::err({Error::Code::ResourceLimit,"native runtime call byte limit"});
  auto valid=backend.verify(kind,dispatch,code);if(!valid)return Result<RuntimeCallCode>::err(valid.error());return result;
}
Result<RuntimeResult> invoke_call(const RuntimeCallCode& code,std::function<Result<RuntimeResult>()> operation){
  RuntimeCallFrame frame{std::move(operation)};code.entry(&frame);
  if(!frame.result||frame.duplicate)return Result<RuntimeResult>::err({Error::Code::Conflict,"native entry must dispatch its runtime frame exactly once"});
  return std::move(*frame.result);
}
std::shared_ptr<NativeProgramStorage> call_storage(const RuntimeCallBackend& backend,const RuntimeCallCode& code){
  auto storage=std::make_shared<NativeProgramStorage>();storage->identity=backend.identity;storage->artifact={code.machine_ir,code.bytes,code.owner};return storage;
}
}
Result<int64_t> recover_integer(const NativeState& state,const NativeLocation& location){
  if(location.kind==NativeLocation::Kind::Constant)return Result<int64_t>::ok(location.constant);
  if(location.kind==NativeLocation::Kind::Register){auto value=state.registers.find(location.reg);if(value==state.registers.end()||!value->second.integer())return Result<int64_t>::err({Error::Code::Conflict,"native recovery register is missing or noninteger"});return Result<int64_t>::ok(*value->second.integer());}
  if(location.kind!=NativeLocation::Kind::Stack)return Result<int64_t>::err({Error::Code::InvalidArgument,"unknown native recovery location"});
  if(state.byte_order!="little"&&state.byte_order!="big")return Result<int64_t>::err({Error::Code::InvalidArgument,"native stack recovery needs explicit byte order"});
  if(location.offset>state.stack.size()||8>state.stack.size()-location.offset)return Result<int64_t>::err({Error::Code::Conflict,"native recovery stack slot is outside safepoint image"});
  uint64_t value=0;for(unsigned k=0;k<8;++k)value|=uint64_t(state.stack[size_t(location.offset)+k])<<(8*(state.byte_order=="little"?k:7-k));return Result<int64_t>::ok(std::bit_cast<int64_t>(value));
}
Result<RuntimeResult> deoptimize(const NativeGuard& input,const NativeState& input_state,const ExecutionOptions& input_options){
  return checked<RuntimeResult>([&]()->Result<RuntimeResult>{
    auto guard=input;auto state=input_state;auto options=input_options;auto valid=validate_guard(guard);if(!valid)return Result<RuntimeResult>::err(valid.error());
    auto condition=recover_integer(state,guard.condition);if(!condition)return Result<RuntimeResult>::err(condition.error());auto snapshot=guard.snapshot;
    if(!guard.values.empty()){std::map<uint32_t,RuntimeValue> values;for(auto& [id,location]:guard.values){
      if(location.kind==NativeLocation::Kind::Register){auto found=state.registers.find(location.reg);if(found==state.registers.end())return Result<RuntimeResult>::err({Error::Code::Conflict,"native recovery value register is missing"});values.emplace(id,found->second);}
      else {auto value=recover_integer(state,location);if(!value)return Result<RuntimeResult>::err(value.error());values.emplace(id,RuntimeValue::integer(value.value()));}}
      auto recovered=recover_guard_values(snapshot,values,guard.recovery_limit);if(!recovered)return Result<RuntimeResult>::err(recovered.error());snapshot=std::move(recovered.value());}
    if(guard.recover){auto recovered=guard.recover(snapshot,state);if(!recovered)return Result<RuntimeResult>::err(recovered.error());if(!recovered.value().valid())return Result<RuntimeResult>::err({Error::Code::Conflict,"native recovery returned an empty continuation"});snapshot=std::move(recovered.value());}
    if(guard.prove){valid=guard.prove(guard.snapshot,snapshot,state);if(!valid)return Result<RuntimeResult>::err(valid.error());}
    return resume_guard(snapshot,condition.value(),options);
  });
}
Result<NativeProgram> lower_native(const RuntimeProgram& input,const NativeRuntimeAdapter& input_adapter){
  return checked<NativeProgram>([&]()->Result<NativeProgram>{
    auto program=input;auto adapter=input_adapter;
    if(!program.valid()||adapter.identity.empty()||!adapter.lower||!adapter.prove)return Result<NativeProgram>::err({Error::Code::InvalidArgument,"native runtime lowering needs an owning program, adapter identity, lowering and semantic proof"});
    auto artifact=adapter.lower(program);if(!artifact)return Result<NativeProgram>::err(artifact.error());auto& result=artifact.value();
    if(!result.owner||!result.invoke||result.bytes.empty()||result.machine_ir.empty())return Result<NativeProgram>::err({Error::Code::Conflict,"native lowering returned incomplete executable/IR ownership"});
    if(result.bytes.size()>adapter.byte_limit||result.guards.size()>adapter.guard_limit)return Result<NativeProgram>::err({Error::Code::ResourceLimit,"native runtime lowering byte/guard limit"});
    std::set<uint32_t> ids;for(auto& guard:result.guards){if(!ids.insert(guard.id).second)return Result<NativeProgram>::err({Error::Code::Conflict,"duplicate native guard identity"});auto valid=validate_guard(guard);if(!valid)return Result<NativeProgram>::err(valid.error());}
    auto valid=adapter.prove(program,result);if(!valid)return Result<NativeProgram>::err(valid.error());auto storage=std::make_shared<NativeProgramStorage>();storage->program=std::move(program);storage->identity=std::move(adapter.identity);storage->artifact=std::move(result);return Result<NativeProgram>::ok(NativeProgram(std::move(storage)));
  });
}
Result<RuntimeResult> run_native(const NativeProgram& program,std::span<const RuntimeValue> input,const ExecutionOptions& input_options){
  return checked<RuntimeResult>([&]()->Result<RuntimeResult>{
    auto active=program.storage_;if(!active||!active->artifact.invoke)return Result<RuntimeResult>::err({Error::Code::InvalidArgument,"native program is not a runtime entry"});std::vector<RuntimeValue> arguments(input.begin(),input.end());auto options=input_options;
    for(auto& value:arguments)if(!value.integer()&&!value.callable())return Result<RuntimeResult>::err({Error::Code::InvalidArgument,"empty native runtime argument"});
    if(options.cancelled&&options.cancelled())return Result<RuntimeResult>::err({Error::Code::Interrupted,"native runtime execution cancelled"});
    auto result=active->artifact.invoke(arguments,options);if(!result)return result;
    if(!result.value().value.integer()&&!result.value().value.callable())return Result<RuntimeResult>::err({Error::Code::Conflict,"native runtime returned an empty value"});
    if(result.value().value.integer()&&*result.value().value.integer()!=result.value().execution.value)return Result<RuntimeResult>::err({Error::Code::Conflict,"native runtime result disagrees with execution record"});
    return result;
  });
}
std::string_view NativeProgram::identity() const {return storage_?std::string_view(storage_->identity):std::string_view{};}
std::string_view NativeProgram::machine_ir() const {return storage_?std::string_view(storage_->artifact.machine_ir):std::string_view{};}
std::span<const uint8_t> NativeProgram::bytes() const {return storage_?std::span<const uint8_t>(storage_->artifact.bytes):std::span<const uint8_t>{};}
Result<NativeProgram> lower_runtime_call(const RuntimeProgram& input,const RuntimeCallBackend& input_backend){
  return checked<NativeProgram>([&]()->Result<NativeProgram>{
    auto source=input;auto backend=input_backend;if(!source.valid())return Result<NativeProgram>::err({Error::Code::InvalidArgument,"empty runtime-call source"});
    auto code=compile_call(RuntimeCallKind::Program,backend);if(!code)return Result<NativeProgram>::err(code.error());auto storage=call_storage(backend,code.value());storage->program=source;
    storage->artifact.invoke=[source,code=std::move(code.value())](auto arguments,const auto& options){return invoke_call(code,[&]{return run_runtime(source,arguments,options);});};
    return Result<NativeProgram>::ok(NativeProgram(std::move(storage)));
  });
}
Result<NativeProgram> lower_runtime_call(const RuntimeValue& input,const RuntimeCallBackend& input_backend){
  return checked<NativeProgram>([&]()->Result<NativeProgram>{
    auto source=input;auto backend=input_backend;if(!source.callable())return Result<NativeProgram>::err({Error::Code::InvalidArgument,"native closure lowering requires a callable owning value"});
    auto code=compile_call(RuntimeCallKind::Closure,backend);if(!code)return Result<NativeProgram>::err(code.error());auto storage=call_storage(backend,code.value());
    storage->artifact.invoke=[source,code=std::move(code.value())](auto arguments,const auto& options){return invoke_call(code,[&]{return apply_runtime(source,arguments,options);});};
    return Result<NativeProgram>::ok(NativeProgram(std::move(storage)));
  });
}
Result<NativeProgram> lower_runtime_call(const NativeGuard& input,const RuntimeCallBackend& input_backend){
  return checked<NativeProgram>([&]()->Result<NativeProgram>{
    auto source=input;auto backend=input_backend;auto valid=validate_guard(source);if(!valid)return Result<NativeProgram>::err(valid.error());
    auto code=compile_call(RuntimeCallKind::Deoptimization,backend);if(!code)return Result<NativeProgram>::err(code.error());auto storage=call_storage(backend,code.value());
    storage->deoptimize=[source,code=std::move(code.value())](const auto& state,const auto& options){return invoke_call(code,[&]{return deoptimize(source,state,options);});};
    return Result<NativeProgram>::ok(NativeProgram(std::move(storage)));
  });
}
Result<RuntimeResult> run_native_deoptimization(const NativeProgram& program,const NativeState& input,const ExecutionOptions& input_options){
  return checked<RuntimeResult>([&]()->Result<RuntimeResult>{auto active=program.storage_;auto state=input;auto options=input_options;
    if(!active||!active->deoptimize)return Result<RuntimeResult>::err({Error::Code::InvalidArgument,"native program is not a deoptimization entry"});
    return active->deoptimize(state,options);
  });
}
}
