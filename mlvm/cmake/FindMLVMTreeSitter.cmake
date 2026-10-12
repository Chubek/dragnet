find_path(MLVMTreeSitter_INCLUDE_DIR tree_sitter/api.h)
find_library(MLVMTreeSitter_LIBRARY NAMES tree-sitter)
set(MLVMTreeSitter_ABI_COMPATIBLE FALSE)
if(MLVMTreeSitter_INCLUDE_DIR)
  file(STRINGS "${MLVMTreeSitter_INCLUDE_DIR}/tree_sitter/api.h" _mlvm_ts_abi
    REGEX "^#define TREE_SITTER_LANGUAGE_VERSION [0-9]+")
  string(REGEX MATCH "[0-9]+" _mlvm_ts_abi "${_mlvm_ts_abi}")
  if(_mlvm_ts_abi GREATER_EQUAL 15)
    set(MLVMTreeSitter_ABI_COMPATIBLE TRUE)
  endif()
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MLVMTreeSitter REQUIRED_VARS
  MLVMTreeSitter_INCLUDE_DIR MLVMTreeSitter_LIBRARY MLVMTreeSitter_ABI_COMPATIBLE)
if(MLVMTreeSitter_FOUND AND NOT TARGET MLVMTreeSitter::Runtime)
  add_library(MLVMTreeSitter::Runtime UNKNOWN IMPORTED)
  set_target_properties(MLVMTreeSitter::Runtime PROPERTIES
    IMPORTED_LOCATION "${MLVMTreeSitter_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MLVMTreeSitter_INCLUDE_DIR}")
endif()
mark_as_advanced(MLVMTreeSitter_INCLUDE_DIR MLVMTreeSitter_LIBRARY)
