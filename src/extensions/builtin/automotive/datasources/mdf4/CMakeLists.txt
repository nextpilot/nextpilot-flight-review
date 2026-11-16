add_library(ps_data_loader_mdf4 STATIC
  Mdf4DataLoader.cpp
)
target_include_directories(ps_data_loader_mdf4 PRIVATE
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
)
target_link_libraries(ps_data_loader_mdf4 PRIVATE ps_loader_common ps_zlib)
