# Compile DOMWeave's existing sfsexp sources without configuring or modifying
# the dependency tree. Optilisp's public headers expose only MLVM-owned types.
add_library(mlvm_sfsexp STATIC ${MLVM_SFSEXP_SOURCES})
target_include_directories(mlvm_sfsexp SYSTEM PUBLIC
  $<BUILD_INTERFACE:${MLVM_SFSEXP_SOURCE_DIR}/src>)
target_compile_definitions(mlvm_sfsexp PRIVATE _NO_MEMORY_MANAGEMENT_)
set_target_properties(mlvm_sfsexp PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED ON
  EXPORT_NAME Sfsexp POSITION_INDEPENDENT_CODE ON)
install(TARGETS mlvm_sfsexp EXPORT ${MLVM_OPTILISP_EXPORT})
install(FILES "${MLVM_SFSEXP_SOURCE_DIR}/COPYING"
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME sfsexp-COPYING)
install(FILES "${MLVM_SFSEXP_SOURCE_DIR}/LICENSE_LGPL"
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME sfsexp-LICENSE_LGPL)
