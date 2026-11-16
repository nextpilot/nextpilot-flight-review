add_subdirectory(data_sources)

# 内置业务组件：只在这里聚合，base 不认识具体组件和格式。
add_subdirectory(video)
add_subdirectory(device_debug)
add_subdirectory(transform)
add_subdirectory(script)

if(PS_BUILD_GNSS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gnss/CMakeLists.txt)
  add_subdirectory(gnss)
endif()

# 聚合 target：base 只链接它；为空时不影响底座构建。
# 组件对象通过 INTERFACE_SOURCES 进入宿主，保证静态注册器不被裁掉。
add_library(ps_builtin_components INTERFACE)
target_sources(ps_builtin_components INTERFACE
  $<TARGET_OBJECTS:ps_builtin_data_sources_component>
  $<TARGET_OBJECTS:ps_builtin_video_component>
  $<TARGET_OBJECTS:ps_builtin_device_debug_component>
  $<TARGET_OBJECTS:ps_builtin_transform_component>
  $<TARGET_OBJECTS:ps_builtin_script_component>
)
target_link_libraries(ps_builtin_components INTERFACE
  ps_builtin_data_sources_component
  ps_builtin_video_component
  ps_builtin_device_debug_component
  ps_builtin_transform_component
  ps_builtin_script_component
)
if(TARGET ps_gnss_component)
  target_sources(ps_builtin_components INTERFACE $<TARGET_OBJECTS:ps_gnss_component>)
  target_link_libraries(ps_builtin_components INTERFACE ps_gnss_component)
endif()
