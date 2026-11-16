# MAVLink generated C library is header-only. Consumers only need include paths
# and message-info metadata; no extra source is compiled here.
add_library(ps_mavlink INTERFACE)
target_include_directories(ps_mavlink INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/generated/ardupilotmega
  ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

# 生成的 msgmap.hpp 会 #include <endian.h>，MSVC 没有这个头，补一个 compat 目录
if (MSVC)
  target_include_directories(ps_mavlink INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
endif()
