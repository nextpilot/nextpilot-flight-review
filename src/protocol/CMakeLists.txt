add_library(ps_protocol STATIC
  ProtocolSpec.cpp
  ProtocolDocument.cpp
)
target_include_directories(ps_protocol PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ps_protocol PUBLIC Qt6::Core)
