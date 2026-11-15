add_library(ps_custom_protocol STATIC
  CustomProtocolLoader.cpp
  ProtocolEditorDialog.cpp
  ProtocolEditorDialog.h
  SequentialCodec.cpp
)
set_target_properties(ps_custom_protocol PROPERTIES AUTOMOC ON)
target_include_directories(ps_custom_protocol PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/protocol
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
)
target_link_libraries(ps_custom_protocol PUBLIC
  Qt6::Widgets
  ps_protocol
  ps_loader_common
)

# 组件注册对象：OBJECT 库保证静态初始化一定进入宿主
add_library(ps_custom_protocol_component OBJECT CustomProtocolComponent.cpp)
set_target_properties(ps_custom_protocol_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_custom_protocol_component PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/protocol
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
)
target_link_libraries(ps_custom_protocol_component PUBLIC ps_custom_protocol ps_protocol ps_host)
