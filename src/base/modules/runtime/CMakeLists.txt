add_library(ps_host STATIC
  src/Component.cpp
  src/DynamicExtension.cpp
  src/ExtensionHost.cpp
  src/ExtensionRuntime.cpp
  src/HostApi.cpp
  src/CommandRegistry.cpp
)
target_include_directories(ps_host PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ps_host PUBLIC Qt6::Core ps_kernel ${CMAKE_DL_LIBS})
ps_set_warnings(ps_host)
