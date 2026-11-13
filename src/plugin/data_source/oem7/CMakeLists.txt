find_package(Qt6 REQUIRED COMPONENTS Network)
add_library(ps_data_source_oem7 STATIC Oem7Source.cpp)
target_include_directories(ps_data_source_oem7 PRIVATE
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
)
target_link_libraries(ps_data_source_oem7 PUBLIC Qt6::Core Qt6::Network)
