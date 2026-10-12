#ifndef VMWEAVE_NATIVE_H
#define VMWEAVE_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct vmweave_native_context vmweave_native_context;
typedef struct vmweave_native_program vmweave_native_program;
typedef struct {
  uint32_t opcode, operand_count;
  uint64_t operands[16];
} vmweave_native_instruction;
typedef struct {
  const char *compiler;
  const char *const *compile_arguments;
  size_t compile_argument_count;
  const char *const *link_arguments;
  size_t link_argument_count;
  uint32_t timeout_seconds;
  size_t instruction_limit, image_limit, execution_budget;
} vmweave_native_options;
/* NULL/zero options select defaults (60 seconds, 4096 instructions, 32 MiB,
   1,000,000 execution steps). Contexts borrow no caller strings. */
vmweave_native_context *vmweave_native_context_create(const vmweave_native_options *,size_t state_size,uint64_t state_abi);
void vmweave_native_context_destroy(vmweave_native_context *);
int vmweave_native_compile(vmweave_native_context *,const char *stk,const vmweave_native_instruction *,size_t count,vmweave_native_program **out);
int vmweave_native_execute(vmweave_native_context *,vmweave_native_program *,void *state);
void vmweave_native_program_destroy(vmweave_native_program *);
/* Borrowed until the next operation on this context. Synchronize a shared
   context; independent contexts/handles own independent compilation state. */
const char *vmweave_native_error(const vmweave_native_context *);
#ifdef __cplusplus
}
#endif
#endif
