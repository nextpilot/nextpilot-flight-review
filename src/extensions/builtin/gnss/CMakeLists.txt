find_package(Qt6 REQUIRED COMPONENTS Network)

# 数据源能力：静态库，测试与宿主都可链接
add_library(ps_gnss STATIC
  NmeaDataLoader.cpp
  UbxDataLoader.cpp
  Oem7Source.cpp
)
set_target_properties(ps_gnss PROPERTIES AUTOMOC ON)
target_include_directories(ps_gnss PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
)
target_link_libraries(ps_gnss PUBLIC
  Qt6::Core
  Qt6::Network
  ps_loader_common
  ps_live
)

# 组件注册对象：OBJECT 库保证静态初始化一定进入宿主
add_library(ps_gnss_component OBJECT GnssComponent.cpp)
set_target_properties(ps_gnss_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_gnss_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
)
target_link_libraries(ps_gnss_component PUBLIC ps_gnss ps_host)
