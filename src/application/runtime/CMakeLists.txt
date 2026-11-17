add_library(ps_runtime INTERFACE)
target_include_directories(ps_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_runtime INTERFACE Qt6::Core)
