#pragma once
#include "bridge.hpp"
#include <map>

namespace limestone::machineir_native {
// Foreign C is an explicit runtime binding, not a guessed instruction semantic.
// The host C toolchain lowers its complete C99 semantics and ABI. The entry CFG,
// SSA values, effects, calls, branches and returns remain ordinary MachineIR.
struct ForeignFunction {
  std::string name, result_type = "i32", body, file;
  uint32_t line = 0;
  std::vector<std::string> argument_types;
};
struct Unit {
  machineir_bridge::RegionExchange entry;
  std::vector<uint32_t> parameters;
  std::string support_c;
  std::map<std::string, ForeignFunction> functions;
  std::map<uint32_t, std::string> callees;
};
struct Options {
  std::string compiler; // empty: the C compiler selected by CMake
  std::vector<std::string> compile_arguments, link_arguments;
  uint32_t timeout_seconds = 60;
  size_t image_limit = 32 * 1024 * 1024;
};
Result<int> verify(const Unit&);
Result<std::string> serialize(const Unit&);
Result<Unit> deserialize(std::string_view, std::string_view file = "<native-machineir>");
Result<std::string> emit_c(const Unit&);
Result<std::string> emit_assembly(const Unit&, Options = {});
struct LibraryStorage;
class Library {
  std::shared_ptr<LibraryStorage> storage_;
  explicit Library(std::shared_ptr<LibraryStorage>);
  friend Result<Library> compile(const Unit&, Options);
public:
  Library() = default;
  Result<void*> symbol(const std::string&) const;
  std::span<const uint8_t> image() const;
  std::string_view target() const;
};
Result<Library> compile(const Unit&, Options = {});
} // namespace limestone::machineir_native
