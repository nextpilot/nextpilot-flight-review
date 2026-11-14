add_library(ps_cache STATIC DatasetCache.cpp)
target_include_directories(ps_cache PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)
target_link_libraries(ps_cache PUBLIC ps_kernel Qt6::Core)
