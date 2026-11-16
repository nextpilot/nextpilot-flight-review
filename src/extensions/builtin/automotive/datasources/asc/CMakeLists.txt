add_library(ps_data_loader_asc STATIC
  AscDataLoader.cpp
  AscParser.cpp
)

target_include_directories(ps_data_loader_asc PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
)

target_link_libraries(ps_data_loader_asc PRIVATE ps_can_common)
