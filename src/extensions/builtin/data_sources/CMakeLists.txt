add_library(ps_builtin_data_sources_component OBJECT
  BuiltinDataSources.cpp
  BuiltinDataSources.h
)
set_target_properties(ps_builtin_data_sources_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_data_sources_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
)
target_link_libraries(ps_builtin_data_sources_component PUBLIC
  ps_host
  ps_kernel
  ps_data_loader_csv
  ps_data_loader_json
  ps_data_loader_binary
)
