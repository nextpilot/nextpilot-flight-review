add_library(ps_datasource_protocol STATIC
  parser/MessageParserRegistry.cpp
  parser/ProtocolCatalog.cpp
  parser/FrameDecoder.cpp
)
target_include_directories(ps_datasource_protocol PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/datasource
)
target_link_libraries(ps_datasource_protocol PUBLIC Qt6::Core)

add_library(ps_application_datasource OBJECT
  include/datasource/DataSourceModule.h
  include/datasource/SourceModel.h
  include/datasource/DataSourceEntry.h
  include/datasource/DataSourceRegistry.h
  include/datasource/ScriptHostBridge.h
  include/datasource/IMessageParser.h
  include/datasource/MessageParserSpec.h
  include/datasource/MessageParserRegistry.h
  include/datasource/DecodedFrame.h
  manager/DataSourceRegistry.cpp
  manager/DataSourceModule.cpp
  manager/ScriptHostBridge.cpp
  manager/DataSourceSession.h
  manager/DataSourceSession.cpp
  manager/DataSourceCommands.cpp
  ribbon/DataSourceRibbon.cpp
  dock/DataSourceDock.h
  dock/DataSourceDock.cpp
  dock/ProtocolInspectorDock.h
  dock/ProtocolInspectorDock.cpp
  dialog/DataSourceDialog.h
  dialog/DataSourceDialog.cpp
  dialog/TransformDialog.h
  dialog/TransformDialog.cpp
  dialog/DeviceDebugDialog.h
  dialog/DeviceDebugDialog.cpp
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
target_link_libraries(ps_application_datasource PUBLIC ps_datasource_protocol ps_host ps_kernel ps_filter_ui ps_live ps_cache Qt6::Widgets Qt6::Concurrent SARibbonBar::SARibbonBar)
