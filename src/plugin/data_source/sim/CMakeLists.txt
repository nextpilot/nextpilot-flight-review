add_library(ps_data_source_sim STATIC SimSource.cpp)

target_include_directories(ps_data_source_sim PRIVATE
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
)
target_link_libraries(ps_data_source_sim PUBLIC Qt6::Core)
