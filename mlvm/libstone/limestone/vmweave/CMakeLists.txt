find_package(Lua 5.4 EXACT QUIET)
if(NOT LUA_FOUND)
  find_package(Lua 5.3 EXACT REQUIRED)
endif()
set(VMWEAVE_LUA_VERSION "${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}" PARENT_SCOPE)
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/kaguya/include/kaguya/kaguya.hpp")
  message(FATAL_ERROR "VMWeave requires the third_party/kaguya binding adapter")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS vmweave.lua)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vmweave.lua" VMWEAVE_LUA_SOURCE)
configure_file(lua_source.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/lua_source.hpp" @ONLY)
add_library(vmweave STATIC model.cpp frontend.cpp stk.cpp c_backend.cpp machineir.cpp native.cpp native_c_api.cpp)
target_link_libraries(vmweave PUBLIC limestone_foundation PRIVATE machineir_bridge machineir_native
  $<BUILD_INTERFACE:${LUA_LIBRARIES}> $<INSTALL_INTERFACE:Limestone::vmweave_lua>)
target_include_directories(vmweave SYSTEM PRIVATE ${LUA_INCLUDE_DIR} "${PROJECT_SOURCE_DIR}/third_party/kaguya/include")
target_include_directories(vmweave PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_executable(vmweave-cli main.cpp)
target_link_libraries(vmweave-cli PRIVATE vmweave)
if(BUILD_TESTING)
  add_executable(vmweave-model-test tests/model.cpp)
  target_link_libraries(vmweave-model-test PRIVATE vmweave machineir_bridge)
  add_test(NAME vmweave-model COMMAND vmweave-model-test)
  add_executable(vmweave-allocators-test tests/allocators.cpp)
  target_link_libraries(vmweave-allocators-test PRIVATE limestone_foundation)
  target_include_directories(vmweave-allocators-test SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/memtkx/include")
  add_test(NAME vmweave-allocators COMMAND vmweave-allocators-test)
  set(native_fixtures "${CMAKE_CURRENT_BINARY_DIR}/native-fixtures")
  add_custom_command(OUTPUT "${native_fixtures}/Calculator.c" "${native_fixtures}/Calculator.h"
    "${native_fixtures}/ObjectVM.c" "${native_fixtures}/ObjectVM.h"
    "${native_fixtures}/Hooked.h"
    COMMAND vmweave-cli --generate -o "${native_fixtures}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/calculator.lua"
    COMMAND vmweave-cli --generate -o "${native_fixtures}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/objectvm.lua"
    COMMAND vmweave-cli --generate -o "${native_fixtures}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_hooks.lua"
    DEPENDS vmweave-cli examples/calculator.lua examples/objectvm.lua tests/native_hooks.lua VERBATIM)
  add_custom_target(vmweave-native-fixtures DEPENDS "${native_fixtures}/Calculator.c" "${native_fixtures}/ObjectVM.c")
  add_executable(vmweave-native-test tests/native.cpp "${native_fixtures}/Calculator.c")
  add_dependencies(vmweave-native-test vmweave-native-fixtures)
  target_include_directories(vmweave-native-test PRIVATE "${native_fixtures}")
  target_link_libraries(vmweave-native-test PRIVATE vmweave machineir_native)
  set_target_properties(vmweave-native-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
  add_test(NAME vmweave-native COMMAND vmweave-native-test "${CMAKE_CURRENT_SOURCE_DIR}")
  add_executable(vmweave-native-c-test tests/native_bridge.c)
  add_dependencies(vmweave-native-c-test vmweave-native-fixtures)
  target_include_directories(vmweave-native-c-test PRIVATE "${native_fixtures}")
  target_link_libraries(vmweave-native-c-test PRIVATE vmweave)
  set_target_properties(vmweave-native-c-test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES LINKER_LANGUAGE CXX)
  target_compile_options(vmweave-native-c-test PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Werror>)
  add_test(NAME vmweave-native-c COMMAND vmweave-native-c-test)
  add_test(NAME vmweave-native-cli COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:vmweave-cli> -DCC=${CMAKE_C_COMPILER}
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_cli.cmake)
  add_test(NAME vmweave-runtime COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:vmweave-cli> -DCC=${CMAKE_C_COMPILER}
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime.cmake)
endif()
