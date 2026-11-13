add_library(ps_runtime STATIC PluginRuntime.cpp)
target_include_directories(ps_runtime PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ps_runtime PUBLIC ${CMAKE_DL_LIBS})
