find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
find_library(LZ4_LIBRARY NAMES lz4 REQUIRED)

add_library(ps_data_load_mcap STATIC McapReader.cpp McapDataLoader.cpp)
target_include_directories(ps_data_load_mcap PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
)
target_link_libraries(ps_data_load_mcap PUBLIC
  Qt6::Core
  ps_loader_common
  ${ZSTD_LIBRARY}
  ${LZ4_LIBRARY}
)
