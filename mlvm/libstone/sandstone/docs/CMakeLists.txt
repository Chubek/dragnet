# Sandstone documentation: Doxygen API reference + prose guides.
find_package(Doxygen QUIET)
if(NOT DOXYGEN_FOUND)
  message(STATUS "Doxygen not found; 'docs' target unavailable")
  return()
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
               ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

add_custom_target(sandstone-docs ALL
  COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating Sandstone documentation with Doxygen"
)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html/
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/sandstone
  OPTIONAL
)
