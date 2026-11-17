set(PS_MODULE_TARGETS "" CACHE INTERNAL "启用的业务模块 target" FORCE)

if(PS_MODULE_DATASOURCE)
  add_subdirectory(datasource)
  list(APPEND PS_MODULE_TARGETS ps_application_datasource)
endif()

if(PS_MODULE_ANALYSIS)
  add_subdirectory(analysis)
  list(APPEND PS_MODULE_TARGETS ps_application_analysis)
endif()

if(PS_MODULE_PLOTTING)
  add_subdirectory(plotting)
  list(APPEND PS_MODULE_TARGETS ps_application_plotting)
endif()

if(PS_MODULE_SCRIPTING)
  add_subdirectory(scripting)
  list(APPEND PS_MODULE_TARGETS ps_application_scripting)
endif()

if(PS_MODULE_PROTOCOL AND PS_BUILD_CUSTOM_PROTOCOL)
  add_subdirectory(protocol)
  list(APPEND PS_MODULE_TARGETS ps_application_protocol)
endif()

set(PS_MODULE_TARGETS "${PS_MODULE_TARGETS}" CACHE INTERNAL "启用的业务模块 target" FORCE)
