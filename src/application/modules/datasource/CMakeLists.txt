add_library(ps_application_datasource OBJECT
  include/datasource/DataSourceModule.h
  include/datasource/SourceModel.h
  include/datasource/DataSourceEntry.h
  include/datasource/DataSourceRegistry.h
  include/datasource/TransformDialogInterface.h
  manager/DataSourceRegistry.cpp
  manager/DataSourceModule.cpp
  manager/DataSourceSession.h
  manager/DataSourceSession.cpp
  manager/DataSourceCommands.cpp
  ribbon/DataSourceRibbon.cpp
  dock/DataSourceDock.h
  dock/DataSourceDock.cpp
  dialog/DataSourceDialog.h
  dialog/DataSourceDialog.cpp
)
set_target_properties(ps_application_datasource PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_datasource PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/datasource
  ${CMAKE_CURRENT_SOURCE_DIR}/manager
  ${CMAKE_CURRENT_SOURCE_DIR}/dock
  ${CMAKE_CURRENT_SOURCE_DIR}/dialog
  ${CMAKE_CURRENT_SOURCE_DIR}/services/live
  ${CMAKE_CURRENT_SOURCE_DIR}/services/cache
  ${PROJECT_SOURCE_DIR}/src/application/shell
)
target_link_libraries(ps_application_datasource PUBLIC ps_host ps_live ps_cache Qt6::Widgets SARibbonBar::SARibbonBar)
