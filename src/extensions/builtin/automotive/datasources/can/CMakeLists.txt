add_library(ps_can_common STATIC
  CanFrames.cpp
  Dbc.cpp
)

target_include_directories(ps_can_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ps_can_common PUBLIC
  ps_loader_common
  Qt6::Core
)
