add_library(ps_datasource_protocol STATIC
  core/parser/MessageParserRegistry.cpp
  core/parser/ProtocolCatalog.cpp
  core/parser/FrameDecoder.cpp
)
target_include_directories(ps_datasource_protocol PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/datasource
)
target_link_libraries(ps_datasource_protocol PUBLIC Qt6::Core)

add_library(ps_application_datasource OBJECT
  include/datasource/DataSourceModule.h
  include/datasource/DataSourceModel.h
  include/datasource/DataSourceConfig.h
  include/datasource/DataSourceDriver.h
  include/datasource/DataSourceEntry.h
  include/datasource/DataSourceRegistry.h
  include/datasource/HostDataBridge.h
  include/datasource/IMessageParser.h
  include/datasource/MessageParserSpec.h
  include/datasource/MessageParserRegistry.h
  include/datasource/DecodedFrame.h
  core/DataSourceController.h
  core/DataSourceController.cpp
  core/DataSourceService.h
  core/DataSourceService.cpp
  manager/DataSourceRegistry.cpp
  manager/DataSourceConfig.cpp
  widget/session/DataSourceModule.cpp
  manager/HostDataBridge.cpp
  widget/session/DataSourceSession.h
  widget/session/DataSourceSession.cpp
  manager/OfflineDataSourceDriver.h
  manager/OfflineDataSourceDriver.cpp
  manager/LiveDataSourceDriver.h
  manager/LiveDataSourceDriver.cpp
  widget/session/DataSourceCommands.cpp
  widget/ribbon/DataSourceRibbon.cpp
  widget/dock/DataSourceDock.h
  widget/dock/DataSourceDock.cpp
  widget/dock/ProtocolInspectorDock.h
  widget/dock/ProtocolInspectorDock.cpp
  widget/dialog/DataSourceDialog.h
  widget/dialog/DataSourceDialog.cpp
  widget/dialog/TransformDialog.h
  widget/dialog/TransformDialog.cpp
  widget/dialog/DeviceDebugDialog.h
  widget/dialog/DeviceDebugDialog.cpp
)
set_target_properties(ps_application_datasource PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_datasource PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/runtime/host/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/datasource
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/core/parser
  ${CMAKE_CURRENT_SOURCE_DIR}/manager
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/session
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/dock
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/dialog
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/ribbon
  ${CMAKE_CURRENT_SOURCE_DIR}/services/live
  ${CMAKE_CURRENT_SOURCE_DIR}/services/cache
  ${PROJECT_SOURCE_DIR}/src/application/shell
)
target_link_libraries(ps_application_datasource PUBLIC ps_datasource_protocol ps_runtime ps_host ps_kernel ps_filter_ui ps_live ps_cache Qt6::Widgets Qt6::Concurrent SARibbonBar::SARibbonBar)
