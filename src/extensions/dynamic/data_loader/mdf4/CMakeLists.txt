add_library(ps_data_loader_mdf4 STATIC
  Mdf4DataLoader.cpp
)
target_include_directories(ps_data_loader_mdf4 PRIVATE
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
)
find_package(ZLIB REQUIRED)
target_link_libraries(ps_data_loader_mdf4 PRIVATE ps_loader_common ZLIB::ZLIB)
