# Sandstone — SymAS-driven assembler framework.
#
# Layout:
#   decoder/    SymAS databank library (lib `sandstone`, magic 0xCEEB09)
#   include/    public C/C++ headers (sandstone.h/.hpp + domsymas shims)
#   compiler/   spec compiler: JSON documents -> .symas banks
#   assembler/  assembler framework + `sandstone-as` CLI
#   bindings/   SWIG interface (built via bindings/generate-bindings.sh)
#   isa/        small `.isa` sources + loaders (Python/C++)
#   docs/       Doxygen + prose documentation
#   scripts/    spec-compiler.py (.dsymspec -> .symas driver) and helpers
cmake_minimum_required(VERSION 3.16)

project(Sandstone VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

option(SANDSTONE_BUILD_COMPILER "Build the spec compiler" ON)
option(SANDSTONE_BUILD_ASSEMBLER "Build the assembler framework and CLI" ON)
option(SANDSTONE_BUILD_TESTS "Build and register tests" ON)
option(SANDSTONE_BUILD_DOCS "Build documentation (needs Doxygen)" OFF)
option(SANDSTONE_BUILD_SANDSLAB "Build the machine-near C++ code generator and Forth example" ON)

if(SANDSTONE_BUILD_TESTS)
  enable_testing()
endif()
if(SANDSTONE_BUILD_SANDSLAB)
  add_subdirectory(sandslab)
endif()

# ---- core databank library ----
add_library(sandstone STATIC
  decoder/domsymas.c
  decoder/abi.cpp
  decoder/archive.cpp
  decoder/binary.cpp
  decoder/compression.cpp
  decoder/container.cpp
  decoder/databank.cpp
  decoder/encoding.cpp
  decoder/format.cpp
  decoder/instr.cpp
  decoder/linking.cpp
  decoder/relocation.cpp
  decoder/semantics.cpp
  decoder/syntax.cpp
  decoder/tuning.cpp
  decoder/validation.cpp
)
# Historical target name kept as an alias.
add_library(domsymas ALIAS sandstone)

target_include_directories(sandstone
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decoder
)
target_compile_options(sandstone PRIVATE -Wall -Wextra)

# zlib carries the default SymAS payload compression; require it so the
# produced banks stay readable everywhere.
find_package(ZLIB REQUIRED)
target_link_libraries(sandstone PUBLIC ZLIB::ZLIB)

# zstd is optional: without it, zstd banks fail with a clean diagnostic
# instead of silently producing unreadable files.
find_library(SANDSTONE_ZSTD_LIB zstd)
find_path(SANDSTONE_ZSTD_INCLUDE zstd.h)
if(SANDSTONE_ZSTD_LIB AND SANDSTONE_ZSTD_INCLUDE)
  target_link_libraries(sandstone PUBLIC ${SANDSTONE_ZSTD_LIB})
  target_include_directories(sandstone PUBLIC ${SANDSTONE_ZSTD_INCLUDE})
  target_compile_definitions(sandstone PUBLIC SANDSTONE_HAS_ZSTD=1)
endif()

find_package(Threads QUIET)
if(Threads_FOUND)
  target_link_libraries(sandstone PUBLIC Threads::Threads)
endif()

install(TARGETS sandstone
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/sandstone.h include/sandstone.hpp
              include/domsymas.h include/domsymas.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY isa/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/sandstone/isa
  FILES_MATCHING PATTERN "*.isa" PATTERN "*.py" PATTERN "*.hpp"
                 PATTERN "*.md")

if(SANDSTONE_BUILD_COMPILER)
  add_subdirectory(compiler)
endif()

if(SANDSTONE_BUILD_ASSEMBLER)
  add_subdirectory(assembler)
endif()

if(SANDSTONE_BUILD_DOCS)
  add_subdirectory(docs)
endif()

# SWIG bindings are script-driven (see bindings/generate-bindings.sh);
# expose them as a convenience target when SWIG is present.
find_program(SWIG_EXECUTABLE swig)
if(SWIG_EXECUTABLE)
  add_custom_target(sandstone-bindings
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/bindings/generate-bindings.sh
            --all --out ${CMAKE_CURRENT_BINARY_DIR}/bindings
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating Sandstone SWIG bindings"
  )
endif()

if(SANDSTONE_BUILD_TESTS)
  include(CTest)
  add_executable(sandstone_smoke tests/smoke.cpp)
  target_link_libraries(sandstone_smoke PRIVATE sandstone)
  target_compile_options(sandstone_smoke PRIVATE -Wall -Wextra)
  add_test(NAME sandstone_smoke COMMAND sandstone_smoke)

  # End-to-end: compile a shipped spec to .symas with the scripted
  # pipeline, then assemble a sample with the freshly built CLI.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND AND SANDSTONE_BUILD_COMPILER
     AND SANDSTONE_BUILD_ASSEMBLER)
    add_test(NAME sandstone_spec_pipeline
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pipeline.py
              --build-dir ${CMAKE_CURRENT_BINARY_DIR}
              --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
    )
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(NAME sandstone_simple_isa
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
              -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p "test_simple_isa.py"
    )
  endif()
  # The C++ simple-ISA harness needs DomDSL; build it
  # only when they are available.
  find_path(DOMDSL_INCLUDE_DIR DomDSL.hpp
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../../domlibs/domdsl)
  if(DOMDSL_INCLUDE_DIR)
    add_executable(sandstone_simple_isa_cpp tests/simple_isa.cpp)
    target_include_directories(sandstone_simple_isa_cpp PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/isa ${DOMDSL_INCLUDE_DIR})
    target_compile_definitions(sandstone_simple_isa_cpp PRIVATE
      SIMPLE_ISA_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/isa")
    add_test(NAME sandstone_simple_isa_cpp COMMAND sandstone_simple_isa_cpp)
  endif()
endif()
