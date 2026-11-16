add_subdirectory(datasources/rosbag2)
add_subdirectory(datasources/mcap)

add_library(ps_robotics_component OBJECT RoboticsComponent.cpp)
set_target_properties(ps_robotics_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_robotics_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
)
target_link_libraries(ps_robotics_component PUBLIC
  ps_host
  ps_data_loader_rosbag2
  ps_data_load_mcap
)
