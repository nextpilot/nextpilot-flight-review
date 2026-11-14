add_library(ps_data_loader_binary STATIC
  BinaryDataLoader.cpp
)
target_include_directories(ps_data_loader_binary PRIVATE
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common
)
target_link_libraries(ps_data_loader_binary PRIVATE ps_loader_common)
