add_subdirectory(datasources/can)
add_subdirectory(datasources/asc)
add_subdirectory(datasources/blf)
add_subdirectory(datasources/mdf4)

add_library(ps_automotive_component OBJECT AutomotiveComponent.cpp)
set_target_properties(ps_automotive_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_automotive_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
)
target_link_libraries(ps_automotive_component PUBLIC
  ps_host
  ps_data_loader_asc
  ps_data_loader_blf
  ps_data_loader_mdf4
)
