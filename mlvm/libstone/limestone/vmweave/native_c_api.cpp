#include "native.h"
#include "native.hpp"
#include <new>
using namespace limestone;
struct vmweave_native_context {
  vmweave::NativeOptions options;
  size_t size=0, budget=1000000;
  uint64_t abi=0;
  std::string error;
};
struct vmweave_native_program { vmweave::NativeProgram program; };
namespace {
int error(vmweave_native_context* ctx,const Error& e) {
  if(ctx) ctx->error=e.message;
  if(e.code==Error::Code::Conflict) return -11;
  if(e.code==Error::Code::Unsupported) return -9;
  if(e.code==Error::Code::ResourceLimit) return -3;
  if(e.code==Error::Code::Timeout) return -13;
  if(e.code==Error::Code::InvalidArgument) return -1;
  return -12;
}
}
extern "C" {
vmweave_native_context* vmweave_native_context_create(const vmweave_native_options* opts,size_t size,uint64_t abi) {
  if(!size) return nullptr;
  try {
    auto ctx=std::make_unique<vmweave_native_context>(); ctx->size=size; ctx->abi=abi;
    if(opts) {
      if((opts->compile_argument_count && !opts->compile_arguments) || (opts->link_argument_count && !opts->link_arguments)) return nullptr;
      if(opts->compiler) ctx->options.compiler=opts->compiler;
      for(size_t i=0;i<opts->compile_argument_count;++i) {if(!opts->compile_arguments[i]) return nullptr; ctx->options.compile_arguments.emplace_back(opts->compile_arguments[i]);}
      for(size_t i=0;i<opts->link_argument_count;++i) {if(!opts->link_arguments[i]) return nullptr; ctx->options.link_arguments.emplace_back(opts->link_arguments[i]);}
      if(opts->timeout_seconds) ctx->options.timeout_seconds=opts->timeout_seconds;
      if(opts->instruction_limit) ctx->options.instruction_limit=opts->instruction_limit;
      if(opts->image_limit) ctx->options.image_limit=opts->image_limit;
      if(opts->execution_budget) ctx->budget=opts->execution_budget;
    }
    return ctx.release();
  } catch(...) {return nullptr;}
}
void vmweave_native_context_destroy(vmweave_native_context* ctx) {delete ctx;}
int vmweave_native_compile(vmweave_native_context* ctx,const char* stk,const vmweave_native_instruction* code,size_t count,vmweave_native_program** out) {
  if(out) *out=nullptr;
  if(!ctx || !stk || !out || (count && !code)) return -1;
  try {
    ctx->error.clear();
    if(count>ctx->options.instruction_limit || count>4096) return error(ctx,{Error::Code::ResourceLimit,"VMWeave native instruction limit"});
    auto module=vmweave::parse_stk(stk); if(!module) return error(ctx,module.error());
    std::vector<vmweave::NativeInstruction> instructions; instructions.reserve(count);
    for(size_t i=0;i<count;++i) {
      if(code[i].operand_count>16) return error(ctx,{Error::Code::InvalidArgument,"VMWeave native operand limit"});
      instructions.push_back({code[i].opcode,std::vector<uint64_t>(code[i].operands,code[i].operands+code[i].operand_count)});
    }
    auto compiled=vmweave::compile_native(module.value(),instructions,ctx->options); if(!compiled) return error(ctx,compiled.error());
    if(compiled.value().state_size()!=ctx->size || compiled.value().state_abi()!=ctx->abi) return error(ctx,{Error::Code::Conflict,"VMWeave native state ABI/layout mismatch"});
    *out=new vmweave_native_program{std::move(compiled.value())}; return 0;
  } catch(const std::bad_alloc&) {return error(ctx,{Error::Code::ResourceLimit,"VMWeave native allocation failure"});}
  catch(const std::exception& e) {return error(ctx,{Error::Code::Internal,e.what()});}
}
int vmweave_native_execute(vmweave_native_context* ctx,vmweave_native_program* program,void* state) {
  if(!ctx || !program) return -1;
  try {
    ctx->error.clear(); auto result=program->program.execute(state,ctx->size,ctx->abi,ctx->budget);
    if(!result) return error(ctx,result.error());
    return result.value();
  } catch(const std::exception& e) {return error(ctx,{Error::Code::Internal,e.what()});}
}
void vmweave_native_program_destroy(vmweave_native_program* program) {delete program;}
const char* vmweave_native_error(const vmweave_native_context* ctx) {return ctx?ctx->error.c_str():"invalid VMWeave native context";}
}
