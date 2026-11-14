find_package(Qt6 REQUIRED COMPONENTS Network)

add_library(ps_gnss STATIC
  GnssComponent.cpp
  GnssComponent.h
  NmeaDataLoader.cpp
  UbxDataLoader.cpp
  Oem7Source.cpp
)
set_target_properties(ps_gnss PROPERTIES AUTOMOC ON)
target_include_directories(ps_gnss PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common
)
target_link_libraries(ps_gnss PUBLIC
  Qt6::Core
  Qt6::Network
  ps_loader_common
)
