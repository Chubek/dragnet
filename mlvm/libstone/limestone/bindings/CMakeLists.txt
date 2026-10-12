find_package(SWIG 4 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
set(LIMESTONE_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)
include(UseSWIG)
set(CMAKE_SWIG_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/python")
set_source_files_properties(limestone.i PROPERTIES CPLUSPLUS ON
  USE_TARGET_INCLUDE_DIRECTORIES TRUE DEPENDS "${PROJECT_SOURCE_DIR}/limestone/limestone.h;${PROJECT_SOURCE_DIR}/limestone/il.h;${PROJECT_SOURCE_DIR}/limestone/runtime.h;${PROJECT_SOURCE_DIR}/limestone/optimization.h;${PROJECT_SOURCE_DIR}/limestone/object.h;${PROJECT_SOURCE_DIR}/limestone/traceml.h")
swig_add_library(limestone_python TYPE MODULE LANGUAGE python SOURCES limestone.i)
target_include_directories(limestone_python PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(limestone_python PRIVATE limestone_core limestone_il limestone_optimization limestone_object limestone_traceml_api Python3::Module)
set_target_properties(limestone_python PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SWIG_OUTDIR}")
set(LIMESTONE_PYTHON_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/limestone/python" CACHE PATH
  "Install directory for the optional Python module")
install(TARGETS limestone_python LIBRARY DESTINATION "${LIMESTONE_PYTHON_INSTALL_DIR}")
install(FILES "${CMAKE_SWIG_OUTDIR}/limestone.py" DESTINATION "${LIMESTONE_PYTHON_INSTALL_DIR}")
if(BUILD_TESTING)
  add_test(NAME python-bindings COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/python_bindings.py" "${PROJECT_SOURCE_DIR}/tests/fixtures")
  set_tests_properties(python-bindings PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_SWIG_OUTDIR}")
  set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/optimization_python.i" PROPERTIES
    CPLUSPLUS ON USE_TARGET_INCLUDE_DIRECTORIES TRUE
    DEPENDS "${PROJECT_SOURCE_DIR}/limestone/optimization.h;${PROJECT_SOURCE_DIR}/limestone/limestone.h")
  swig_add_library(optimization_fixture TYPE MODULE LANGUAGE python
    SOURCES "${PROJECT_SOURCE_DIR}/tests/optimization_python.i")
  target_include_directories(optimization_fixture PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(optimization_fixture PRIVATE limestone_optimization Python3::Module)
  set_target_properties(optimization_fixture PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SWIG_OUTDIR}")
  add_test(NAME python-optimization-adapter COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/python_optimization.py" "${PROJECT_SOURCE_DIR}/tests/fixtures")
  set_tests_properties(python-optimization-adapter PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_SWIG_OUTDIR}")
endif()
