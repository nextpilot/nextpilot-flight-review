add_library(ps_loader_csv STATIC
  CsvPlugin.cpp
)

target_include_directories(ps_loader_csv PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ps_loader_csv PRIVATE ps_kernel Qt6::Core)
