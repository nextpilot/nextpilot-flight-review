add_library(ps_data_loader_tlog STATIC TlogDataLoader.cpp)

target_include_directories(ps_data_loader_tlog PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/builtin/autopilot
)

target_link_libraries(ps_data_loader_tlog PRIVATE ps_loader_common ps_autopilot Qt6::Core)
