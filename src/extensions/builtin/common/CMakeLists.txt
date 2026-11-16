add_subdirectory(loader)
add_subdirectory(datasources/csv)
add_subdirectory(datasources/json)
add_subdirectory(datasources/binary)

# 通用数据源注册组件：实现仍在 datasources/，这里只负责登记 CSV/JSON/binary。
add_library(ps_common_datasources_component OBJECT
  CommonDataSourcesComponent.cpp
  CommonDataSourcesComponent.h
)
set_target_properties(ps_common_datasources_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_common_datasources_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
)
target_link_libraries(ps_common_datasources_component PUBLIC
  ps_host
  ps_kernel
  ps_data_loader_csv
  ps_data_loader_json
  ps_data_loader_binary
)
