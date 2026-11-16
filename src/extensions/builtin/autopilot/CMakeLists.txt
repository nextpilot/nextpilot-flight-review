add_subdirectory(datasources/ulog)
add_subdirectory(datasources/apbin)
add_subdirectory(datasources/mavlink)
add_subdirectory(datasources/tlog)
add_subdirectory(datasources/sim)

add_library(ps_autopilot STATIC
  MavlinkMessageParser.cpp
  MavlinkMessageParser.h
  MavlinkChannelCollector.cpp
  MavlinkChannelCollector.h
  UlogMessageParser.cpp
  UlogMessageParser.h
  ApbinMessageParser.cpp
  ApbinMessageParser.h
)
target_include_directories(ps_autopilot PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application/modules/datasource/include
  ${PROJECT_SOURCE_DIR}/src/application/modules/datasource/include/datasource
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
)
target_link_libraries(ps_autopilot PUBLIC
  Qt6::Core
  ps_datasource_protocol
  ps_mavlink
)
target_compile_definitions(ps_autopilot PRIVATE
  PS_MAVLINK_METADATA_PATH="${PROJECT_SOURCE_DIR}/src/3rdparty/mavlink/generated/mavlink_metadata.json"
)

add_library(ps_autopilot_component OBJECT AutopilotComponent.cpp)
set_target_properties(ps_autopilot_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_autopilot_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
)
target_link_libraries(ps_autopilot_component PUBLIC
  ps_autopilot
  ps_host
  ps_data_loader_ulog
  ps_data_loader_apbin
  ps_data_loader_tlog
  ps_data_source_sim
  ps_data_source_mavlink
)
