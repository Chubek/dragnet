cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SandSLAB VERSION 1.0.0 LANGUAGES CXX)
  include(CTest)
endif()
include(GNUInstallDirs)

find_path(DOMDSL_INCLUDE_DIR DomDSL.hpp
  HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../domlibs/domdsl"
        /mnt/domweave/domlibs/domdsl)
if(NOT DOMDSL_INCLUDE_DIR)
  message(FATAL_ERROR "SandSLAB needs DomDSL.hpp; set DOMDSL_INCLUDE_DIR to its directory")
endif()

add_library(sandslab INTERFACE)
add_library(Sandstone::sandslab ALIAS sandslab)
target_compile_features(sandslab INTERFACE cxx_std_20)
target_include_directories(sandslab INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../isa>
  $<BUILD_INTERFACE:${DOMDSL_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sandslab>)

add_executable(forthcc examples/FORTHCC.cpp)
target_link_libraries(forthcc PRIVATE sandslab)
target_compile_definitions(forthcc PRIVATE
  SANDSLAB_ISA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/sandstone/isa")

if(SANDSTONE_BUILD_TESTS OR BUILD_TESTING)
  add_executable(sandslab_test tests.cpp)
  target_link_libraries(sandslab_test PRIVATE sandslab)
  target_compile_definitions(sandslab_test PRIVATE
    SANDSLAB_TEST_ISA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../isa")
  add_test(NAME sandslab COMMAND sandslab_test)
endif()

install(TARGETS sandslab forthcc EXPORT SandSLABTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT SandSLABTargets FILE SandSLABConfig.cmake NAMESPACE Sandstone::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SandSLAB)
install(FILES SandSLAB.hpp ../isa/simple_isa.hpp "${DOMDSL_INCLUDE_DIR}/DomDSL.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sandslab)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  install(DIRECTORY ../isa/ DESTINATION ${CMAKE_INSTALL_DATADIR}/sandstone/isa
    FILES_MATCHING PATTERN "*.isa")
endif()
