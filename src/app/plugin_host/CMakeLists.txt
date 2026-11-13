add_library(ps_plugin_host STATIC PluginRuntime.cpp HostApi.cpp)
target_include_directories(ps_plugin_host PUBLIC ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/plugin/include)
target_link_libraries(ps_plugin_host PUBLIC ${CMAKE_DL_LIBS} ps_kernel)
ps_set_warnings(ps_plugin_host)
