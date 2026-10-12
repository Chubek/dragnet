set(stage "${BINARY}/installed-original")
set(prefix "${BINARY}/installed-relocated")
file(REMOVE_RECURSE "${stage}" "${prefix}" "${BINARY}/installed-consumer")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BINARY}" --prefix "${stage}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Installation failed: ${output}${error}")
endif()
file(RENAME "${stage}" "${prefix}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer" -B "${BINARY}/installed-consumer"
  "-DCMAKE_PREFIX_PATH=${prefix}" "-DCMAKE_C_COMPILER=${CC}" "-DCMAKE_CXX_COMPILER=${CXX}"
  "-DCMAKE_C_FLAGS=${C_FLAGS}" "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}" "-DCMAKE_EXE_LINKER_FLAGS=${LINK_FLAGS}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Installed consumer configuration failed: ${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BINARY}/installed-consumer" -j 2
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Installed consumer build failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Installed consumer execution failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-implementation-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Implementation-guard consumer execution failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-optimization-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Standalone optimization consumer failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-selection-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Standalone selection consumer execution failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-object-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Standalone object consumer failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-infobank-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Standalone Infobank consumer failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-traceml-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Standalone TraceML runtime consumer failed: ${output}${error}")
endif()
execute_process(COMMAND "${BINARY}/installed-consumer/limestone-vmweave-consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Standalone VMWeave consumer failed: ${output}${error}")
endif()
execute_process(COMMAND "${prefix}/${BIN_DIR}/vmweave-cli" --check
  "${prefix}/${DATA_DIR}/limestone/vmweave/examples/calculator.lua"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Relocated VMWeave generator failed: ${output}${error}")
endif()
execute_process(COMMAND "${prefix}/${BIN_DIR}/vmweave-cli" --native
  --program "${prefix}/${DATA_DIR}/limestone/vmweave/examples/calculator.tape"
  -o "${BINARY}/installed-consumer/native"
  "${prefix}/${DATA_DIR}/limestone/vmweave/examples/calculator.lua"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Relocated VMWeave native compiler failed: ${output}${error}")
endif()
execute_process(COMMAND "${prefix}/${BIN_DIR}/limeburg-generate-specs" --check
  "${prefix}/${DATA_DIR}/limestone/infobank" "${prefix}/${DATA_DIR}/limestone/limeburg/specs"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Relocated Infobank specifications failed: ${output}${error}")
endif()
if(PYTHON_BINDINGS)
  set(python_dir "$ENV{LIMESTONE_TEST_PYTHON_INSTALL_DIR}")
  if(NOT IS_ABSOLUTE "${python_dir}")
    set(python_dir "${prefix}/${python_dir}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${python_dir}"
    "$ENV{LIMESTONE_TEST_PYTHON}" "${SOURCE}/tests/python_bindings.py" "${SOURCE}/tests/fixtures"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Relocated Python bindings failed: ${output}${error}")
  endif()
endif()
