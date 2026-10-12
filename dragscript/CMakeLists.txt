cmake_minimum_required(VERSION 3.20)
project(Dragscript VERSION 0.1.0 LANGUAGES C CXX)

option(DRAGSCRIPT_BUILD_TESTS "Build headless Dragscript tests" ON)
option(DRAGSCRIPT_DIFFERENTIAL_TESTS "Build the vendored QuickJS test oracle" OFF)
option(DRAGSCRIPT_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(DRAGSCRIPT_FUZZING "Enable AFL++ fuzzing build" OFF)
option(DRAGSCRIPT_BUILD_REPL "Build the Dragscript REPL" ON)
option(DRAGSCRIPT_BUILD_REPL_TESTS "Build the headless Dragscript REPL tests" ON)
set(DRAGSCRIPT_DEPENDENCY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Dragnet source root")

if(DRAGSCRIPT_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(NOT TARGET MLVM::IR)
  set(MLVM_BUILD_TOOLS OFF)
  set(MLVM_BUILD_TESTS OFF)
  set(MLVM_ENABLE_CEPHYR OFF CACHE STRING "Build optional MLVM C frontend")
  set(MLVM_ENABLE_BINUTILS OFF CACHE STRING "Build optional MLVM binary tools")
  set(MLVM_ENABLE_OPTILISP OFF CACHE STRING "Build optional MLVM Optilisp")
  add_subdirectory("${DRAGSCRIPT_DEPENDENCY_ROOT}/mlvm" dependencies/mlvm EXCLUDE_FROM_ALL)
endif()

set(TS_ROOT "${DRAGSCRIPT_DEPENDENCY_ROOT}/third_party/tree-sitter")
set(TST_ROOT "${DRAGSCRIPT_DEPENDENCY_ROOT}/third_party/tree-sitter-typescript")
if(NOT TARGET tree-sitter)
  if(EXISTS "${TS_ROOT}/lib/src/lib.c")
    add_library(tree-sitter STATIC "${TS_ROOT}/lib/src/lib.c")
    target_include_directories(tree-sitter PUBLIC "${TS_ROOT}/lib/include" PRIVATE "${TS_ROOT}/lib/src")
    target_compile_definitions(tree-sitter PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE)
    set_target_properties(tree-sitter PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TREE_SITTER REQUIRED IMPORTED_TARGET tree-sitter>=0.25)
    add_library(tree-sitter ALIAS PkgConfig::TREE_SITTER)
    message(STATUS "Vendored Tree-sitter runtime absent; using installed ${TREE_SITTER_VERSION}")
  endif()
endif()
# Upstream's grammar CMake invokes its generator. Build the checked-in C instead.
foreach(grammar typescript tsx)
  if(NOT TARGET tree-sitter-${grammar})
    add_library(tree-sitter-${grammar} STATIC
      "${TST_ROOT}/${grammar}/src/parser.c" "${TST_ROOT}/${grammar}/src/scanner.c")
    target_include_directories(tree-sitter-${grammar} PRIVATE "${TST_ROOT}/${grammar}/src")
    set_target_properties(tree-sitter-${grammar} PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  endif()
endforeach()

add_library(dragscript STATIC
  src/engine.cpp src/source.cpp src/frontend/frontend.cpp
  src/semantic/dsir.cpp src/semantic/validator.cpp
  src/runtime/value.cpp src/runtime/session.cpp src/runtime/interpreter.cpp
  src/mlvm/dialect.cpp src/mlvm/lowering.cpp src/mlvm/tree_interpreter.cpp)
add_library(Dragscript::Core ALIAS dragscript)
target_compile_features(dragscript PUBLIC cxx_std_20)
target_include_directories(dragscript PUBLIC include PRIVATE src "${TST_ROOT}/bindings/c")
target_link_libraries(dragscript PRIVATE MLVM::IR tree-sitter tree-sitter-typescript tree-sitter-tsx)
if(MSVC)
  target_compile_options(dragscript PRIVATE /W4)
else()
  target_compile_options(dragscript PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(DRAGSCRIPT_BUILD_TESTS)
  enable_testing()
  add_executable(dragscript-tests
    tests/tests.cpp tests/frontend_tests.cpp tests/runtime_tests.cpp tests/backend_tests.cpp
    tests/embedding_tests.cpp tests/limits_tests.cpp tests/lowering_tests.cpp tests/fuzz_tests.cpp)
  target_link_libraries(dragscript-tests PRIVATE Dragscript::Core MLVM::IR)
  target_include_directories(dragscript-tests PRIVATE src tests)
  foreach(group frontend runtime backend embedding limits lowering fuzz-smoke)
    add_test(NAME dragscript-${group} COMMAND dragscript-tests ${group})
  endforeach()
  if(DRAGSCRIPT_DIFFERENTIAL_TESTS)
    set(QJS_ROOT "${DRAGSCRIPT_DEPENDENCY_ROOT}/third_party/quickjs")
    add_library(dragscript-quickjs-oracle STATIC
      "${QJS_ROOT}/quickjs.c" "${QJS_ROOT}/dtoa.c"
      "${QJS_ROOT}/libregexp.c" "${QJS_ROOT}/libunicode.c")
    target_include_directories(dragscript-quickjs-oracle PUBLIC "${QJS_ROOT}")
    target_compile_definitions(dragscript-quickjs-oracle PRIVATE _GNU_SOURCE QUICKJS_NG_BUILD)
    target_link_libraries(dragscript-quickjs-oracle PUBLIC m)
    add_executable(dragscript-differential tests/differential.cpp)
    target_link_libraries(dragscript-differential PRIVATE Dragscript::Core dragscript-quickjs-oracle)
    add_test(NAME dragscript-differential COMMAND dragscript-differential)
  endif()
endif()

if(DRAGSCRIPT_FUZZING)
  add_subdirectory(fuzz)
endif()

if(DRAGSCRIPT_BUILD_REPL)
  # Include replxx from third_party
  if(NOT TARGET replxx)
    add_subdirectory("${DRAGSCRIPT_DEPENDENCY_ROOT}/third_party/replxx" replxx)
  endif()

  add_executable(dragscript-repl
    repl/src/main.cpp
    repl/src/repl.cpp
    repl/src/session.cpp
    repl/src/completion.cpp
    repl/src/highlighter.cpp
  )
  target_compile_features(dragscript-repl PUBLIC cxx_std_20)
  target_include_directories(dragscript-repl
    PUBLIC repl/include
    PRIVATE repl/src
    ${TST_ROOT}/bindings/c
  )
  target_link_libraries(dragscript-repl
    PRIVATE Dragscript::Core
    PRIVATE replxx::replxx
  )
  if(MSVC)
    target_compile_options(dragscript-repl PRIVATE /W4)
  else()
    target_compile_options(dragscript-repl PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(DRAGSCRIPT_BUILD_REPL_TESTS)
    add_executable(dragscript-repl-tests
      repl/tests/tests.cpp repl/src/repl.cpp repl/src/session.cpp
      repl/src/completion.cpp repl/src/highlighter.cpp)
    target_link_libraries(dragscript-repl-tests PRIVATE Dragscript::Core replxx::replxx)
    target_include_directories(dragscript-repl-tests
      PRIVATE repl/src repl/include "${DRAGSCRIPT_DEPENDENCY_ROOT}/dragscript/tests")
    add_test(NAME dragscript-repl-tests COMMAND dragscript-repl-tests)
  endif()
endif()
