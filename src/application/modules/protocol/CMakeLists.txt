add_library(ps_protocol STATIC
  protocol/ProtocolSpec.cpp
  protocol/ProtocolDocument.cpp
)
target_include_directories(ps_protocol PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/protocol
)
target_link_libraries(ps_protocol PUBLIC Qt6::Core)

# 协议模块：协议文档、编辑器、codec 与自定义协议 loader 都在这一层，
# 由 plotscope_app 组装；shell 与 datasource/plotting 不 link 它。
add_library(ps_application_protocol OBJECT
  include/protocol/ProtocolModule.h
  manager/ProtocolModule.cpp
  manager/CustomProtocolLoader.cpp
  dialog/ProtocolEditorDialog.h
  dialog/ProtocolEditorDialog.cpp
  codec/SequentialCodec.hpp
  codec/SequentialCodec.cpp
)
set_target_properties(ps_application_protocol PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_protocol PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/dialog
  ${CMAKE_CURRENT_SOURCE_DIR}/codec
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/extensions/builtin/common/loader
)
target_link_libraries(ps_application_protocol PUBLIC
  ps_host
  ps_protocol
  ps_loader_common
  Qt6::Widgets
)
ps_set_warnings(ps_application_protocol)
