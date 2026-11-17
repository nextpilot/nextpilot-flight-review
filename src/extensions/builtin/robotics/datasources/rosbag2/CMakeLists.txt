find_package(Qt6 REQUIRED COMPONENTS Core Sql)
add_library(ps_data_loader_rosbag2 STATIC
  Rosbag2DataLoader.cpp
)
target_include_directories(ps_data_loader_rosbag2 PRIVATE
  ${PROJECT_SOURCE_DIR}/src/application/runtime/host/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/builtin/common/loader
)
target_link_libraries(ps_data_loader_rosbag2 PRIVATE ps_loader_common Qt6::Core Qt6::Sql)
