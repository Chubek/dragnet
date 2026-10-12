# Sandstone spec compiler: JSON spec documents -> SymAS (.symas) banks.
#
# Layers:
#   sandstone_spec_core     JSON spec document -> domsymas::Builder (no Lua)
#   sandstone-spec-compiler dependency-free CLI driver (always built)
#   domsymascc              legacy DomMune-embedded CLI (main.cpp), built only
#                           when SANDSTONE_WITH_MUNE is ON and the DomMune /
#                           DomCLI sources are provided (they are not part of
#                           this repository).
#
# The .dsymspec -> JSON step is performed by scripts/spec-compiler.py with
# stock Lua (the dsl.lua DSL is Lua 5.4 compatible); the JSON -> .symas step
# is this directory.

option(SANDSTONE_WITH_MUNE "Build the legacy DomMune-embedded domsymascc CLI" OFF)

# ---- spec core (JSON document -> Builder), no interpreter required ----
add_library(sandstone_spec_core STATIC
  src/spec_document.cpp
)
target_include_directories(sandstone_spec_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(sandstone_spec_core PUBLIC sandstone)
target_compile_options(sandstone_spec_core PRIVATE -Wall -Wextra)
# Backwards-compatible alias for the historical target name.
add_library(domsymascc_core ALIAS sandstone_spec_core)

# ---- dependency-free CLI ----
add_executable(sandstone-spec-compiler src/spec_compiler.cpp)
target_link_libraries(sandstone-spec-compiler PRIVATE sandstone_spec_core)
target_compile_options(sandstone-spec-compiler PRIVATE -Wall -Wextra)

install(TARGETS sandstone-spec-compiler sandstone_spec_core
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/domsymascc/domsymascc.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/domsymascc
)
install(DIRECTORY stdspec/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/sandstone/stdspec
  FILES_MATCHING PATTERN "*.dsymspec" PATTERN "*.lua" PATTERN "README.md"
)

# ---- legacy DomMune-embedded driver (optional, needs external sources) ----
if(SANDSTONE_WITH_MUNE)
  if(NOT DEFINED DOMWEAVE_ROOT)
    if(EXISTS "${CMAKE_SOURCE_DIR}/domainlet" AND EXISTS "${CMAKE_SOURCE_DIR}/domimpl")
      set(DOMWEAVE_ROOT "${CMAKE_SOURCE_DIR}")
    else()
      get_filename_component(DOMWEAVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    endif()
  endif()
  if(NOT TARGET mune_core)
    add_subdirectory("${DOMWEAVE_ROOT}/domimpl/dommune"
                     "${CMAKE_CURRENT_BINARY_DIR}/dommune" EXCLUDE_FROM_ALL)
  endif()
  add_executable(domsymascc src/main.cpp)
  target_include_directories(domsymascc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DOMWEAVE_ROOT}/domlibs/domcli
  )
  target_link_libraries(domsymascc PRIVATE sandstone_spec_core mune_core)
  target_compile_options(domsymascc PRIVATE -Wall -Wextra)
  install(TARGETS domsymascc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
