#pragma once
#include "native_internal.h"
#include <memory>
#include <string>
#include <vector>

struct exl_data_type_snapshot {
  bool scalar=true,array=false;
  exl_native_kind_t kind=EXL_NATIVE_VOID;
  size_t nodes=1,depth=1;
  // Children precede the descriptor so the borrowing ffi_type is destroyed first.
  std::vector<std::shared_ptr<const exl_data_type_snapshot>> children;
  std::unique_ptr<exl_ffi_data_type,decltype(&exl_ffi_data_type_destroy)> native{nullptr,exl_ffi_data_type_destroy};
  bool custom=false;
  std::string identity;
  size_t size=0,alignment=0;
  std::vector<size_t> offsets;
};
struct exl_native_type {
  std::shared_ptr<const exl_data_type_snapshot> snapshot;
};
