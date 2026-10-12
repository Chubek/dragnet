cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(MLVMOptilisp VERSION 0.1.0 LANGUAGES C CXX)
  option(MLVM_BUILD_TESTS "Build Optilisp tests" ON)
  include(CTest)
  include(GNUInstallDirs)
  set(MLVM_OPTILISP_EXPORT MLVMOptilispTargets)
else()
  set(MLVM_OPTILISP_EXPORT MLVMTargets)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/MLVMDependencies.cmake")
if(NOT MLVM_SFSEXP_AVAILABLE)
  message(FATAL_ERROR "Optilisp requires the supplied sfsexp sources: missing ${MLVM_SFSEXP_MISSING_FILE}")
endif()
find_package(Threads REQUIRED)
if(NOT TARGET mlvm_nodeir)
  add_library(mlvm_nodeir INTERFACE)
  add_library(MLVM::NodeIR ALIAS mlvm_nodeir)
  target_compile_features(mlvm_nodeir INTERFACE cxx_std_20)
  target_include_directories(mlvm_nodeir INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  set_target_properties(mlvm_nodeir PROPERTIES EXPORT_NAME NodeIR)
  install(TARGETS mlvm_nodeir EXPORT ${MLVM_OPTILISP_EXPORT})
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/MLVMSfsexp.cmake")

add_library(mlvm_optilisp STATIC sexpr.cpp)
add_library(MLVM::Optilisp ALIAS mlvm_optilisp)
target_compile_features(mlvm_optilisp PUBLIC cxx_std_20)
target_include_directories(mlvm_optilisp PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(mlvm_optilisp PRIVATE mlvm_sfsexp Threads::Threads)
set_target_properties(mlvm_optilisp PROPERTIES EXPORT_NAME Optilisp POSITION_INDEPENDENT_CODE ON)
if(COMMAND mlvm_warnings)
  mlvm_warnings(mlvm_optilisp)
endif()
install(TARGETS mlvm_optilisp EXPORT ${MLVM_OPTILISP_EXPORT})
add_subdirectory(loader)
if(MLVM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  install(DIRECTORY ../include/mlvm/optilisp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mlvm
    PATTERN "pass.hpp" EXCLUDE)
  install(DIRECTORY ../include/mlvm/NodeIR DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mlvm)
  include(CMakePackageConfigHelpers)
  configure_package_config_file(MLVMOptilispConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MLVMOptilispConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MLVMOptilisp)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/MLVMOptilispConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
  install(EXPORT MLVMOptilispTargets NAMESPACE MLVM::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MLVMOptilisp)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/MLVMOptilispConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MLVMOptilispConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MLVMOptilisp)
endif()
