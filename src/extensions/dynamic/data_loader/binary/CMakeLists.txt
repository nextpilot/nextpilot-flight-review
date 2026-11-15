add_library(ps_data_loader_binary STATIC
  BinaryDataLoader.cpp
)
target_include_directories(ps_data_loader_binary PRIVATE
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
)
target_link_libraries(ps_data_loader_binary PRIVATE ps_loader_common)
