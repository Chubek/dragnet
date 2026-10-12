# Public API layout mirrors the source tree, including relative header includes.
set(limestone_install_targets limestone_foundation limestone_parsers metacode
  schedrow regtl regtl_schedrow limeburg unisel limeburg_umd limeburg_infobank
  limeburg_text schedrow_text regtl_text bin2bin bin2bin_codegen bin2bin_object machineir_bridge machineir_native
  tunah tunah_unisel tunah_bin2bin tunah_stages traceml exolayer vmweave limestone_core limestone_il limestone_optimization limestone_object limestone_traceml_api limestone_selection_contracts)
foreach(target IN LISTS limestone_install_targets)
  string(REGEX REPLACE "^limestone_" "" public_name "${target}")
  set_target_properties(${target} PROPERTIES EXPORT_NAME ${public_name})
  add_library(Limestone::${public_name} ALIAS ${target})
endforeach()
# These static implementations are link dependencies, never public parser types.
list(APPEND limestone_install_targets limestone_parser_tables limestone_dparse)
install(TARGETS ${limestone_install_targets} limestone-cli limeburg-generate-specs vmweave-cli EXPORT LimestoneTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
foreach(component IN ITEMS limestone metacode schedrow regtl limeburg unisel bin2bin tunah traceml exolayer parsers)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/${component}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${component}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
    PATTERN "infobank" EXCLUDE PATTERN "source" EXCLUDE PATTERN "tests" EXCLUDE
    PATTERN "tuners" EXCLUDE PATTERN "backend" EXCLUDE)
endforeach()
install(FILES ${PROJECT_SOURCE_DIR}/vmweave/vmweave.hpp ${PROJECT_SOURCE_DIR}/vmweave/native.hpp ${PROJECT_SOURCE_DIR}/vmweave/native.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vmweave)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/vmweave/adapters/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vmweave/adapters
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(FILES ${limestone_ast_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/parsers)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/exolangtk/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/limestone/vendor/exolangtk
  FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/metacode/infobank/ DESTINATION ${CMAKE_INSTALL_DATADIR}/limestone/infobank)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/tunah/tuners/ DESTINATION ${CMAKE_INSTALL_DATADIR}/limestone/tuners)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/limeburg/specs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/limestone/limeburg/specs)
install(FILES ${PROJECT_SOURCE_DIR}/vmweave/vmweave.lua DESTINATION ${CMAKE_INSTALL_DATADIR}/limestone/vmweave)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/vmweave/examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/limestone/vmweave/examples)
install(FILES ${PROJECT_SOURCE_DIR}/vmweave/STK-00.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone/vmweave)
install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/IMPLEMENTATION.md ${PROJECT_SOURCE_DIR}/LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/manual/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone/manual
  FILES_MATCHING PATTERN "*.md")
foreach(component IN ITEMS limestone metacode/machine-ir schedrow regtl limeburg unisel bin2bin tunah traceml exolayer vmweave bindings parsers)
  install(FILES ${PROJECT_SOURCE_DIR}/${component}/README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone/${component})
endforeach()
install(FILES ${PROJECT_SOURCE_DIR}/tunah/BENCHMARKS.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone/tunah)
install(FILES ${PROJECT_SOURCE_DIR}/bin2bin/OBJECTS.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone/bin2bin)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/exolangtk/docs/manual/19_exolayer.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/exolangtk/docs/manual/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/limestone/third_party/exolangtk/docs/manual
  FILES_MATCHING PATTERN "*.md")
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/LimestoneConfig.cmake.in
  ${PROJECT_BINARY_DIR}/LimestoneConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Limestone)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/LimestoneConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT LimestoneTargets NAMESPACE Limestone:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Limestone)
install(FILES ${PROJECT_BINARY_DIR}/LimestoneConfig.cmake ${PROJECT_BINARY_DIR}/LimestoneConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Limestone)
