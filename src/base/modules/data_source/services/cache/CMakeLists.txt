add_library(ps_cache STATIC DatasetCache.cpp)
target_include_directories(ps_cache PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ps_cache PUBLIC ps_kernel Qt6::Core)
