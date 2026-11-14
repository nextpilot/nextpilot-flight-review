add_library(ps_custom_protocol STATIC
  CustomProtocolComponent.cpp
  CustomProtocolLoader.cpp
  ProtocolEditorDialog.cpp
  ProtocolEditorDialog.h
  SequentialCodec.cpp
)
set_target_properties(ps_custom_protocol PROPERTIES AUTOMOC ON)
target_include_directories(ps_custom_protocol PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common
)
target_link_libraries(ps_custom_protocol PUBLIC
  Qt6::Widgets
  ps_protocol
  ps_loader_common
)
