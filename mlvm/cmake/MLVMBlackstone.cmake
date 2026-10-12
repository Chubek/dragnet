# MLVM-owned catalog tooling; libstone is a read-only source dependency.
if(MLVM_BUILD_TESTS OR MLVM_BUILD_TOOLS)
  find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    if(MLVM_BUILD_TESTS)
      add_test(NAME mlvm-blackstone-tests
        COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
          -s tools/blackstone/tests -v)
      add_test(NAME mlvm-blackstone-catalog
        COMMAND ${Python3_EXECUTABLE} -B -m tools.blackstone validate)
      set_tests_properties(mlvm-blackstone-tests mlvm-blackstone-catalog
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(MLVM_BUILD_TOOLS)
      add_custom_target(mlvm-blackstone-validate
        COMMAND ${Python3_EXECUTABLE} -B -m tools.blackstone validate
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Validate MLVM's 50 supplemental Blackstone kernels")
    endif()
  endif()
endif()
