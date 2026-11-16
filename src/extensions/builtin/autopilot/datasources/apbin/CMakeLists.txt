add_library(ps_data_loader_apbin STATIC
  ApbinDataLoader.cpp
  ApbinParser.cpp
)

target_include_directories(ps_data_loader_apbin PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
)

target_link_libraries(ps_data_loader_apbin PRIVATE ps_loader_common)
