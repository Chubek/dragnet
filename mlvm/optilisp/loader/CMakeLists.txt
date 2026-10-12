add_library(mlvm_optilisp_loader STATIC loader.cpp rewrite.cpp)
add_library(MLVM::OptilispLoader ALIAS mlvm_optilisp_loader)
target_link_libraries(mlvm_optilisp_loader PUBLIC MLVM::Optilisp MLVM::NodeIR)
set_target_properties(mlvm_optilisp_loader PROPERTIES EXPORT_NAME OptilispLoader
  POSITION_INDEPENDENT_CODE ON)
if(COMMAND mlvm_warnings)
  mlvm_warnings(mlvm_optilisp_loader)
endif()
install(TARGETS mlvm_optilisp_loader EXPORT ${MLVM_OPTILISP_EXPORT})

if(TARGET mlvm_ir)
  add_library(mlvm_optilisp_pass STATIC pass.cpp integer.cpp)
  add_library(MLVM::OptilispPass ALIAS mlvm_optilisp_pass)
  target_link_libraries(mlvm_optilisp_pass PUBLIC MLVM::OptilispLoader MLVM::IR)
  set_target_properties(mlvm_optilisp_pass PROPERTIES EXPORT_NAME OptilispPass
    POSITION_INDEPENDENT_CODE ON)
  mlvm_warnings(mlvm_optilisp_pass)
  install(TARGETS mlvm_optilisp_pass EXPORT ${MLVM_OPTILISP_EXPORT})
endif()
