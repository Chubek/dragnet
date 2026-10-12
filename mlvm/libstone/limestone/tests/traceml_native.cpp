#include "test.hpp"
#include "traceml/native.hpp"
#include <bit>
#include <cstring>
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#endif
using namespace limestone;
using namespace limestone::traceml;

int main(){return test_main([]{
  ExecutionOptions guards;guards.record_guards=true;
  auto prepare=[](std::string_view source){return take(lower_runtime(take(compile(source))));};
  auto runtime=prepare("(lambda x (add 40 (if (lt x 0) (neg x) x)))");
  std::vector<RuntimeValue> arguments{RuntimeValue::integer(-2)};
  auto result=take(run_runtime(runtime,arguments,guards));CHECK(result.value.integer()==42&&result.guards.size()==1);
  NativeGuard guard;guard.snapshot=result.guards[0];guard.condition={NativeLocation::Kind::Register,1};
  NativeState state;state.registers[1]=RuntimeValue::integer(0);
  CHECK(take(deoptimize(guard,state)).value.integer()==38);take(lower_trace(take(deoptimize(guard,state)).execution));
  guard.condition={NativeLocation::Kind::Stack,0,1};state.stack={0,1,0,0,0,0,0,0,0};state.byte_order="little";
  CHECK(take(deoptimize(guard,state)).value.integer()==42);state.byte_order="big";CHECK(take(recover_integer(state,guard.condition))==int64_t{1}<<56);
  state.byte_order="unknown";fails(recover_integer(state,guard.condition),Error::Code::InvalidArgument);state.byte_order="little";state.stack.resize(8);fails(deoptimize(guard,state),Error::Code::Conflict);
  guard.condition={NativeLocation::Kind::Register,1};state.registers[1]=RuntimeValue::integer(1);
  auto slots=take(guard_value_slots(guard.snapshot));uint32_t lexical=UINT32_MAX,strict=UINT32_MAX;
  for(auto& slot:slots){if(slot.path.ends_with(".x"))lexical=slot.id;if(slot.path=="continuations[0].values[0]")strict=slot.id;}
  CHECK(lexical!=UINT32_MAX&&strict!=UINT32_MAX);CHECK(take(guard_value_slots(guard.snapshot))[lexical].path==slots[lexical].path);
  guard.values[lexical]={NativeLocation::Kind::Register,7};guard.values[strict]={NativeLocation::Kind::Stack,0,0};
  state.registers[7]=RuntimeValue::integer(-3);state.stack={39,0,0,0,0,0,0,0};
  fails(deoptimize(guard,state),Error::Code::InvalidArgument);
  guard.prove=[](auto&,auto&,auto&){return Result<int>::ok(0);};result=take(deoptimize(guard,state));CHECK(result.value.integer()==42);take(lower_trace(result.execution));
  guard.prove=[](auto&,auto&,auto&){return Result<int>::err({Error::Code::Conflict,"recovery proof rejected"});};fails(deoptimize(guard,state),Error::Code::Conflict);
  guard.prove=[](auto&,auto&,auto&){return Result<int>::ok(0);};guard.recovery_limit=1;fails(deoptimize(guard,state),Error::Code::ResourceLimit);guard.recovery_limit=1000000;
  fails(recover_guard_values(guard.snapshot,{{UINT32_MAX,RuntimeValue::integer(0)}}),Error::Code::InvalidArgument);
  auto callable=take(run_runtime(prepare("(lambda z z)"))).value;fails(recover_guard_values(guard.snapshot,{{strict,callable}}),Error::Code::Conflict);
  RuntimeCallBackend incomplete;fails(lower_runtime_call(runtime,incomplete),Error::Code::InvalidArgument);
  NativeRuntimeAdapter direct;direct.identity="incomplete";fails(lower_native(runtime,direct),Error::Code::InvalidArgument);fails(run_native({}),Error::Code::InvalidArgument);
#if defined(__linux__) && defined(__x86_64__)
  // Explicit Linux x86-64 SysV backend: preserve the frame in rdi and tail-call
  // the supplied runtime dispatcher through movabs rax / jmp rax. These ISA/ABI
  // facts belong to this test backend, never to the TraceML frontend.
  struct Mapping {void* address;size_t size;~Mapping(){munmap(address,size);}};
  size_t lowerings=0;
  auto encoded=[](RuntimeCallEntry dispatch){auto address=reinterpret_cast<uintptr_t>(dispatch);std::vector<uint8_t> bytes{0x48,0xb8};for(unsigned k=0;k<8;++k)bytes.push_back(uint8_t(address>>(8*k)));bytes.insert(bytes.end(),{0xff,0xe0});return bytes;};
  RuntimeCallBackend backend;backend.identity="fixture:x86-64-sysv-runtime-call:1";
  backend.lower=[&](auto kind,auto dispatch){++lowerings;auto bytes=encoded(dispatch);auto address=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(address!=MAP_FAILED);auto owner=std::shared_ptr<Mapping>(new Mapping{address,4096});
    std::memcpy(address,bytes.data(),bytes.size());CHECK(mprotect(address,4096,PROT_READ|PROT_EXEC)==0);
    return Result<RuntimeCallCode>::ok({"module traceml { call.runtime "+std::to_string(int(kind))+" frame; ret; }",std::move(bytes),std::move(owner),reinterpret_cast<RuntimeCallEntry>(address)});};
  backend.verify=[&](auto,auto dispatch,const auto& code){CHECK(code.bytes==encoded(dispatch));CHECK(std::memcmp(reinterpret_cast<const void*>(code.entry),code.bytes.data(),code.bytes.size())==0);return Result<int>::ok(0);};
  // A divergent source proves compilation does not evaluate the program.
  auto divergent=take(lower_runtime_call(prepare("((lambda x (x x)) (lambda x (x x)))"),backend));ExecutionOptions budget;budget.step_limit=50;fails(run_native(divergent,{},budget),Error::Code::ResourceLimit);
  auto native=take(lower_runtime_call(prepare("(lambda x (lambda y (add x y)))"),backend));runtime={};arguments[0]=RuntimeValue::integer(10);
  auto closure=take(run_native(native,arguments)).value;CHECK(closure.callable());auto native_closure=take(lower_runtime_call(closure,backend));native={};closure={};arguments[0]=RuntimeValue::integer(32);CHECK(take(run_native(native_closure,arguments)).value.integer()==42);
  auto lexical_native=take(lower_runtime_call(prepare("(lambda f (f 20 22))"),backend));auto primitive=take(run_runtime(prepare("add"))).value;arguments[0]=primitive;CHECK(take(run_native(lexical_native,arguments)).value.integer()==42);
  for(auto source:{"((lambda x 42) (add 9223372036854775807 1))","(begin (lambda x x) (if 0 (add 9223372036854775807 1) 42))"})CHECK(take(run_native(take(lower_runtime_call(prepare(source),backend)))).value.integer()==42);
  auto pending=take(run_runtime(prepare("((if 1 (lambda x (add x 2)) (lambda x 0)) 40)"),{},guards));NativeGuard pending_guard;pending_guard.snapshot=pending.guards[0];pending_guard.condition={NativeLocation::Kind::Register,1};
  auto exit=take(lower_runtime_call(pending_guard,backend));pending={};pending_guard={};CHECK(take(run_native_deoptimization(exit,state)).value.integer()==42);state.registers[1]=RuntimeValue::integer(0);CHECK(take(run_native_deoptimization(exit,state)).value.integer()==0);
  auto sequence=take(run_runtime(prepare("(begin (if 1 10 20) 30) 42"),{},guards));pending_guard.snapshot=sequence.guards[0];pending_guard.condition={NativeLocation::Kind::Constant,0,0,0};exit=take(lower_runtime_call(pending_guard,backend));CHECK(take(run_native_deoptimization(exit,{})).value.integer()==42);
  auto mapped=take(lower_runtime_call(guard,backend));CHECK(take(run_native_deoptimization(mapped,state)).value.integer()==36);state.registers[1]=RuntimeValue::integer(1);CHECK(take(run_native_deoptimization(mapped,state)).value.integer()==42);
  fails(run_native(exit),Error::Code::InvalidArgument);fails(run_native_deoptimization(native_closure,state),Error::Code::InvalidArgument);
  auto limited=backend;limited.byte_limit=1;fails(lower_runtime_call(prepare("42"),limited),Error::Code::ResourceLimit);
  auto invalid=backend;invalid.verify=[](auto,auto,const auto&){return Result<int>::err({Error::Code::Conflict,"unproved frame ABI"});};fails(lower_runtime_call(prepare("42"),invalid),Error::Code::Conflict);
  NativeProgram active=take(lower_runtime_call(prepare("42"),backend));ExecutionOptions destruction;destruction.observer=[&](auto&){active={};};CHECK(take(run_native(active,{},destruction)).value.integer()==42&&!active.bytes().size());
  CHECK(lowerings>=12);
#endif
});}
