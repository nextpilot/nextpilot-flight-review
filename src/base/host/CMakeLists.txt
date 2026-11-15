add_library(ps_host STATIC
  extension/Component.cpp
  extension/DynamicExtension.cpp
  extension/ExtensionHost.cpp
  extension/ExtensionRuntime.cpp
  extension/HostApi.cpp
  extension/DataInputRegistry.cpp
  extension/CommandRegistry.cpp
)

target_include_directories(ps_host PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/host
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)

target_link_libraries(ps_host PUBLIC
  Qt6::Core
  ps_kernel
  ${CMAKE_DL_LIBS}
)

ps_set_warnings(ps_host)
