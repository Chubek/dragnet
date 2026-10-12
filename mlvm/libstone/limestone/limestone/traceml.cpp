#include "traceml.h"
#include "c_api_internal.hpp"
#include "traceml/traceml.hpp"
#include "traceml_native.h"
#include "traceml/native.hpp"

struct limestone_traceml_runtime { limestone::traceml::RuntimeProgram program; };
struct limestone_traceml_value { limestone::traceml::RuntimeValue value; };
struct limestone_traceml_guard { limestone::traceml::GuardSnapshot guard; };
struct limestone_traceml_result {
  limestone::traceml::RuntimeResult result;
  std::string trace;
  explicit limestone_traceml_result(limestone::traceml::RuntimeResult value):result(std::move(value)),trace(limestone::traceml::print_trace(result.execution)){}
};
struct limestone_traceml_native_backend {limestone::traceml::RuntimeCallBackend backend;};
struct limestone_traceml_native_program {limestone::traceml::NativeProgram program;};
struct limestone_traceml_native_state {limestone::traceml::NativeState state;};
namespace {
using namespace limestone;
using namespace limestone::c_api_internal;
traceml::ExecutionOptions options(const limestone_traceml_options *input) {
  traceml::ExecutionOptions result;if(!input)return result;
  for(auto flag:{input->record_trace,input->record_guards})if(flag!=0&&flag!=1)throw Error{Error::Code::InvalidArgument,"TraceLambda options must be Boolean"};
  result.step_limit=input->step_limit;result.event_limit=input->event_limit;result.record_trace=input->record_trace;result.record_guards=input->record_guards;
  if(input->cancelled)result.cancelled=[callback=input->cancelled,userdata=input->userdata]{return callback(userdata)!=0;};return result;
}
std::vector<traceml::RuntimeValue> arguments(const limestone_traceml_value *const *input,size_t count) {
  if((count&&!input)||count>65536)throw Error{Error::Code::InvalidArgument,"invalid TraceLambda runtime arguments"};std::vector<traceml::RuntimeValue> result;result.reserve(count);
  for(size_t k=0;k<count;++k){if(!input[k])throw Error{Error::Code::InvalidArgument,"null TraceLambda runtime argument"};result.push_back(input[k]->value);}return result;
}
struct NativeOwner {
  void *userdata=nullptr;void (*release)(void *)=nullptr;
  ~NativeOwner(){if(release)release(userdata);}
};
void callback_status(limestone_status status,const limestone_error& error){
  if(status!=LIMESTONE_OK){auto code=status>=LIMESTONE_INVALID_ARGUMENT&&status<=LIMESTONE_RESOURCE_LIMIT?static_cast<Error::Code>(status-1):Error::Code::Internal;auto end=std::find(std::begin(error.message),std::end(error.message),'\0');throw Error{code,error.message[0]?std::string(error.message,end):"TraceML native backend callback failed"};}
}
traceml::NativeLocation location(const limestone_traceml_native_location *input){
  if(!input||input->kind<LIMESTONE_TRACEML_NATIVE_REGISTER||input->kind>LIMESTONE_TRACEML_NATIVE_CONSTANT)throw Error{Error::Code::InvalidArgument,"invalid native recovery location"};
  return {static_cast<traceml::NativeLocation::Kind>(input->kind),input->reg,input->offset,input->constant};
}
}
extern "C" void limestone_traceml_options_default(limestone_traceml_options *output){if(output)*output={100000,100000,1,0,nullptr,nullptr};}
extern "C" limestone_traceml_runtime *limestone_traceml_prepare(const char *source,size_t nodes,limestone_error *error){return boundary(error,[&]()->limestone_traceml_runtime*{if(!source)throw Error{Error::Code::InvalidArgument,"null TraceML source"};return new limestone_traceml_runtime{checked(traceml::lower_runtime(checked(traceml::compile(source)),nodes))};});}
extern "C" void limestone_traceml_runtime_destroy(limestone_traceml_runtime *runtime){delete runtime;}
extern "C" limestone_traceml_value *limestone_traceml_integer(int64_t value,limestone_error *error){return boundary(error,[&]{return new limestone_traceml_value{traceml::RuntimeValue::integer(value)};});}
extern "C" void limestone_traceml_value_destroy(limestone_traceml_value *value){delete value;}
extern "C" limestone_status limestone_traceml_value_integer(const limestone_traceml_value *value,int64_t *output,limestone_error *error){return boundary(error,[&]{if(!value||!output)throw Error{Error::Code::InvalidArgument,"null TraceLambda integer inspection"};auto integer=value->value.integer();if(!integer)throw Error{Error::Code::Unsupported,"TraceLambda value is a function"};*output=*integer;return LIMESTONE_OK;});}
extern "C" int limestone_traceml_value_callable(const limestone_traceml_value *value){return value&&value->value.callable();}
extern "C" limestone_traceml_result *limestone_traceml_invoke(const limestone_traceml_runtime *runtime,const limestone_traceml_value *const *input,size_t count,const limestone_traceml_options *config,limestone_error *error){return boundary(error,[&]()->limestone_traceml_result*{if(!runtime)throw Error{Error::Code::InvalidArgument,"null TraceLambda runtime"};return new limestone_traceml_result(checked(traceml::run_runtime(runtime->program,arguments(input,count),options(config))));});}
extern "C" limestone_traceml_result *limestone_traceml_apply(const limestone_traceml_value *value,const limestone_traceml_value *const *input,size_t count,const limestone_traceml_options *config,limestone_error *error){return boundary(error,[&]()->limestone_traceml_result*{if(!value)throw Error{Error::Code::InvalidArgument,"null TraceLambda function"};return new limestone_traceml_result(checked(traceml::apply_runtime(value->value,arguments(input,count),options(config))));});}
extern "C" limestone_traceml_value *limestone_traceml_result_value(const limestone_traceml_result *result,limestone_error *error){return boundary(error,[&]()->limestone_traceml_value*{if(!result)throw Error{Error::Code::InvalidArgument,"null TraceLambda result"};return new limestone_traceml_value{result->result.value};});}
extern "C" size_t limestone_traceml_result_guard_count(const limestone_traceml_result *result){return result?result->result.guards.size():0;}
extern "C" limestone_traceml_guard *limestone_traceml_result_guard(const limestone_traceml_result *result,size_t index,limestone_error *error){return boundary(error,[&]()->limestone_traceml_guard*{if(!result||index>=result->result.guards.size())throw Error{Error::Code::InvalidArgument,"TraceLambda guard index out of range"};return new limestone_traceml_guard{result->result.guards[index]};});}
extern "C" const char *limestone_traceml_result_trace(const limestone_traceml_result *result){return result?result->trace.c_str():nullptr;}
extern "C" void limestone_traceml_result_destroy(limestone_traceml_result *result){delete result;}
extern "C" int limestone_traceml_guard_expected(const limestone_traceml_guard *guard){return guard?int(guard->guard.expected()):-1;}
extern "C" void limestone_traceml_guard_destroy(limestone_traceml_guard *guard){delete guard;}
extern "C" limestone_traceml_result *limestone_traceml_resume(const limestone_traceml_guard *guard,int64_t condition,const limestone_traceml_options *config,limestone_error *error){return boundary(error,[&]()->limestone_traceml_result*{if(!guard)throw Error{Error::Code::InvalidArgument,"null TraceLambda guard"};return new limestone_traceml_result(checked(traceml::resume_guard(guard->guard,condition,options(config))));});}
extern "C" limestone_traceml_native_backend *limestone_traceml_native_backend_create(const char *identity,size_t limit,limestone_traceml_native_lower lower,limestone_traceml_native_verify verify,void *userdata,void (*release)(void *),limestone_error *error){return boundary(error,[&]()->limestone_traceml_native_backend*{
  if(!identity||!*identity||!lower||!verify)throw Error{Error::Code::InvalidArgument,"native backend requires identity, lowering and verification"};
  auto owner=std::make_shared<NativeOwner>();owner->userdata=userdata;traceml::RuntimeCallBackend backend;backend.identity=identity;backend.byte_limit=limit;
  backend.lower=[owner,lower,limit](auto kind,auto dispatch)->Result<traceml::RuntimeCallCode>{auto code_owner=std::make_shared<NativeOwner>();limestone_traceml_native_code output{};limestone_error error{};callback_status(lower(static_cast<limestone_traceml_native_kind>(kind),dispatch,owner->userdata,&output,&error),error);
    code_owner->userdata=output.userdata;code_owner->release=output.release;
    if(!output.machine_ir||!output.entry||!output.bytes||!output.byte_count)throw Error{Error::Code::Conflict,"incomplete native code descriptor"};
    if(output.byte_count>limit)throw Error{Error::Code::ResourceLimit,"native backend code byte limit"};
    traceml::RuntimeCallCode code{output.machine_ir,{output.bytes,output.bytes+output.byte_count},std::move(code_owner),output.entry};return Result<traceml::RuntimeCallCode>::ok(std::move(code));};
  backend.verify=[owner,verify](auto kind,auto dispatch,const auto& code)->Result<int>{auto code_owner=std::static_pointer_cast<const NativeOwner>(code.owner);limestone_traceml_native_code output{code.machine_ir.c_str(),code.bytes.data(),code.bytes.size(),code.entry,code_owner->userdata,code_owner->release};limestone_error error{};callback_status(verify(static_cast<limestone_traceml_native_kind>(kind),dispatch,&output,owner->userdata,&error),error);return Result<int>::ok(0);};
  auto result=std::make_unique<limestone_traceml_native_backend>(limestone_traceml_native_backend{std::move(backend)});owner->release=release;return result.release();});}
extern "C" void limestone_traceml_native_backend_destroy(limestone_traceml_native_backend *backend){delete backend;}
extern "C" limestone_traceml_native_program *limestone_traceml_compile_native(const limestone_traceml_runtime *source,const limestone_traceml_native_backend *backend,limestone_error *error){return boundary(error,[&]()->limestone_traceml_native_program*{if(!source||!backend)throw Error{Error::Code::InvalidArgument,"null native runtime source/backend"};return new limestone_traceml_native_program{checked(traceml::lower_runtime_call(source->program,backend->backend))};});}
extern "C" limestone_traceml_native_program *limestone_traceml_compile_native_closure(const limestone_traceml_value *source,const limestone_traceml_native_backend *backend,limestone_error *error){return boundary(error,[&]()->limestone_traceml_native_program*{if(!source||!backend)throw Error{Error::Code::InvalidArgument,"null native closure/backend"};return new limestone_traceml_native_program{checked(traceml::lower_runtime_call(source->value,backend->backend))};});}
extern "C" limestone_traceml_native_program *limestone_traceml_compile_native_guard(const limestone_traceml_guard *source,const limestone_traceml_native_location *input,const limestone_traceml_native_backend *backend,limestone_error *error){return boundary(error,[&]()->limestone_traceml_native_program*{if(!source||!backend)throw Error{Error::Code::InvalidArgument,"null native guard/backend"};traceml::NativeGuard guard;guard.snapshot=source->guard;guard.condition=location(input);return new limestone_traceml_native_program{checked(traceml::lower_runtime_call(guard,backend->backend))};});}
extern "C" limestone_traceml_result *limestone_traceml_native_invoke(const limestone_traceml_native_program *program,const limestone_traceml_value *const *input,size_t count,const limestone_traceml_options *config,limestone_error *error){return boundary(error,[&]()->limestone_traceml_result*{if(!program)throw Error{Error::Code::InvalidArgument,"null native runtime program"};return new limestone_traceml_result(checked(traceml::run_native(program->program,arguments(input,count),options(config))));});}
extern "C" limestone_traceml_result *limestone_traceml_native_deoptimize(const limestone_traceml_native_program *program,const limestone_traceml_native_state *state,const limestone_traceml_options *config,limestone_error *error){return boundary(error,[&]()->limestone_traceml_result*{if(!program||!state)throw Error{Error::Code::InvalidArgument,"null native deoptimization program/state"};return new limestone_traceml_result(checked(traceml::run_native_deoptimization(program->program,state->state,options(config))));});}
extern "C" const char *limestone_traceml_native_identity(const limestone_traceml_native_program *program){return program?program->program.identity().data():nullptr;}
extern "C" const char *limestone_traceml_native_machineir(const limestone_traceml_native_program *program){return program?program->program.machine_ir().data():nullptr;}
extern "C" const uint8_t *limestone_traceml_native_bytes(const limestone_traceml_native_program *program,size_t *count){if(count)*count=program?program->program.bytes().size():0;return program?program->program.bytes().data():nullptr;}
extern "C" void limestone_traceml_native_program_destroy(limestone_traceml_native_program *program){delete program;}
extern "C" limestone_traceml_native_state *limestone_traceml_native_state_create(limestone_error *error){return boundary(error,[]{return new limestone_traceml_native_state;});}
extern "C" limestone_status limestone_traceml_native_state_set_register(limestone_traceml_native_state *state,uint32_t reg,const limestone_traceml_value *value,limestone_error *error){return boundary(error,[&]{if(!state||!value)throw Error{Error::Code::InvalidArgument,"null native state/register value"};state->state.registers[reg]=value->value;return LIMESTONE_OK;});}
extern "C" limestone_status limestone_traceml_native_state_set_stack(limestone_traceml_native_state *state,const uint8_t *bytes,size_t count,const char *order,limestone_error *error){return boundary(error,[&]{if(!state||(count&&!bytes)||!order||(std::string_view(order)!="little"&&std::string_view(order)!="big"))throw Error{Error::Code::InvalidArgument,"invalid native stack safepoint image"};if(count>64*1024*1024)throw Error{Error::Code::ResourceLimit,"native stack image byte limit"};std::vector<uint8_t> copy;if(count)copy.assign(bytes,bytes+count);state->state.stack=std::move(copy);state->state.byte_order=order;return LIMESTONE_OK;});}
extern "C" void limestone_traceml_native_state_destroy(limestone_traceml_native_state *state){delete state;}
