# Sandstone assembler framework: library + `sandstone-as` CLI.
add_library(sandstone_assembler STATIC
  assembler.c
  parser.c
  preprocess.c
  directives.c
  binarizer.c
  lua.c
)
target_include_directories(sandstone_assembler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sandstone>
)
target_link_libraries(sandstone_assembler PUBLIC sandstone)
target_compile_options(sandstone_assembler PRIVATE -Wall -Wextra)
# dlopen() backend for the optional Lua hook; no link-time Lua needed.
find_library(SANDSTONE_DL_LIB dl)
if(SANDSTONE_DL_LIB)
  target_link_libraries(sandstone_assembler PRIVATE ${SANDSTONE_DL_LIB})
endif()

add_executable(sandstone-as driver.c)
target_link_libraries(sandstone-as PRIVATE sandstone_assembler)
target_compile_options(sandstone-as PRIVATE -Wall -Wextra)

install(TARGETS sandstone-as sandstone_assembler
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES assembler.h assembler.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sandstone
)
