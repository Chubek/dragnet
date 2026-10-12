set(work "${BINARY}/objects-elf32-interop")
file(MAKE_DIRECTORY "${work}")
file(WRITE "${work}/input.s" ".text\n.globl object32_entry\n.type object32_entry,@function\nobject32_entry:\ncall object32_helper\nret\n.size object32_entry,.-object32_entry\n.data\n.globl object32_pointer\n.type object32_pointer,@object\nobject32_pointer:\n.long object32_entry+3\n.size object32_pointer,4\n.section .note.GNU-stack,\"\",@progbits\n")
execute_process(COMMAND "${CC}" -m32 -c "${work}/input.s" -o "${work}/input.o"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ELF32 assembly failed: ${output}${error}")
endif()
execute_process(COMMAND "${OBJECT_TEST}" "${FIXTURES}" "${work}/input.o" "${work}/reemitted.o"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ELF32 ingestion/link/emission failed: ${output}${error}")
endif()
execute_process(COMMAND "${CC}" -m32 -nostdlib -no-pie -Wl,--build-id=none -Wl,-e,object32_entry -Wl,--defsym,object32_helper=4160
  "${work}/reemitted.o" -o "${work}/linked"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "System linker rejected reemitted ELF32: ${output}${error}")
endif()
