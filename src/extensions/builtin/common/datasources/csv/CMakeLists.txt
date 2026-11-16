add_library(ps_data_loader_csv STATIC
  CsvDataLoader.cpp
  CsvParser.cpp
)

target_include_directories(ps_data_loader_csv PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
)

target_link_libraries(ps_data_loader_csv PRIVATE ps_loader_common)
