#pragma once
#include "vmweave.hpp"

namespace limestone::vmweave {
// Operands are exact cell bit patterns, including sign extension for signed
// cells. Native compilation owns a snapshot; later source tape mutation has no
// effect on the handle.
struct NativeInstruction { uint32_t opcode=0; std::vector<uint64_t> operands; };
struct NativeOptions {
  std::string compiler;
  std::vector<std::string> compile_arguments, link_arguments;
  uint32_t timeout_seconds=60;
  size_t instruction_limit=4096, image_limit=32*1024*1024;
};
Result<std::vector<NativeInstruction>> assemble(const Module&,std::string_view text);
// Built-in foreign-runtime-call lowering. The returned native-C envelope owns
// an ordinary validated MachineIR CFG and all authoritative C runtime bindings.
Result<std::string> lower_native(const Module&,std::span<const NativeInstruction>,NativeOptions = {});
Result<std::string> emit_native_assembly(const Module&,std::span<const NativeInstruction> = {},NativeOptions = {});
struct NativeStorage;
class NativeProgram {
  std::shared_ptr<NativeStorage> storage_;
  explicit NativeProgram(std::shared_ptr<NativeStorage>);
  friend Result<NativeProgram> compile_native(const Module&,std::span<const NativeInstruction>,NativeOptions);
public:
  NativeProgram()=default;
  Result<int> execute(void* state,size_t state_size,uint64_t state_abi,size_t budget) const;
  size_t state_size() const;
  uint64_t state_abi() const;
  std::string_view machine_ir() const;
  std::string_view target() const;
  std::span<const uint8_t> image() const;
};
Result<NativeProgram> compile_native(const Module&,std::span<const NativeInstruction>,NativeOptions = {});
} // namespace limestone::vmweave
