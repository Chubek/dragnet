# Resolve dependencies relative to this file so standalone and composed builds
# share DOMWeave's root third_party tree, independent of CMAKE_SOURCE_DIR.
get_filename_component(MLVM_DEFAULT_THIRD_PARTY_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../../third_party" ABSOLUTE)
set(MLVM_THIRD_PARTY_DIR "${MLVM_DEFAULT_THIRD_PARTY_DIR}"
  CACHE PATH "DOMWeave root dependency source directory")
set(MLVM_SFSEXP_SOURCE_DIR "${MLVM_THIRD_PARTY_DIR}/sfsexp"
  CACHE PATH "Existing sfsexp source directory (never fetched or regenerated)")

set(MLVM_SFSEXP_SOURCES)
foreach(source cstring.c event_temp.c faststack.c io.c parser.c sexp.c
               sexp_memory.c sexp_ops.c sexp_vis.c)
  list(APPEND MLVM_SFSEXP_SOURCES "${MLVM_SFSEXP_SOURCE_DIR}/src/${source}")
endforeach()
set(MLVM_SFSEXP_AVAILABLE ON)
set(MLVM_SFSEXP_MISSING_FILE "")
foreach(file IN LISTS MLVM_SFSEXP_SOURCES ITEMS
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_errors.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_memory.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_ops.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/sexp_vis.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/cstring.h"
    "${MLVM_SFSEXP_SOURCE_DIR}/src/faststack.h")
  if(NOT EXISTS "${file}")
    set(MLVM_SFSEXP_AVAILABLE OFF)
    set(MLVM_SFSEXP_MISSING_FILE "${file}")
    break()
  endif()
endforeach()
