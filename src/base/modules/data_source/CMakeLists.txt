add_library(ps_base_data_source OBJECT
  include/DataSourceModule.h
  include/SourceModel.h
  include/DataSourceEntry.h
  include/DataSourceRegistry.h
  src/DataSourceRegistry.cpp
  src/DataSourceModule.cpp
  include/DataSourceDock.h
  src/DataSourceDock.cpp
  ui/DataSourceDialog.h
  ui/DataSourceDialog.cpp
  ui/DialogEngine.cpp
)
set_target_properties(ps_base_data_source PROPERTIES AUTOMOC ON)
target_include_directories(ps_base_data_source PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/services/live
  ${PROJECT_SOURCE_DIR}/src/base/modules/shell/ui
  ${PROJECT_SOURCE_DIR}/src/base/modules/plot/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/plot/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/services/cache
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)
target_link_libraries(ps_base_data_source PUBLIC ps_host ps_live Qt6::Widgets)

