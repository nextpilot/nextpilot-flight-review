add_library(ps_analysis_registry STATIC
  manager/AnalysisRegistry.cpp
)
target_include_directories(ps_analysis_registry PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ps_analysis_registry PUBLIC Qt6::Core)

add_library(ps_application_analysis OBJECT
  include/analysis/AnalysisModule.h
  manager/AnalysisModule.cpp
  ribbon/AnalysisRibbon.cpp
)
set_target_properties(ps_application_analysis PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_analysis PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/shell
  ${PROJECT_SOURCE_DIR}/src/application/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ps_application_analysis PUBLIC
  ps_analysis_registry
  ps_runtime
  ps_host
  Qt6::Widgets
  SARibbonBar::SARibbonBar
)
