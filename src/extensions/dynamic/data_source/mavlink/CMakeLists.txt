find_package(Qt6 REQUIRED COMPONENTS Network)
add_library(ps_data_source_mavlink STATIC MavlinkSource.cpp)

target_include_directories(ps_data_source_mavlink PRIVATE
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
)
target_link_libraries(ps_data_source_mavlink PUBLIC Qt6::Core Qt6::Network ps_live)
