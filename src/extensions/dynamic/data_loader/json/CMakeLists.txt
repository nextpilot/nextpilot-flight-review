add_library(ps_data_loader_json STATIC
  JsonDataLoader.cpp
)
target_include_directories(ps_data_loader_json PRIVATE
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
)
target_link_libraries(ps_data_loader_json PRIVATE ps_loader_common)
