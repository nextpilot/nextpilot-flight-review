add_subdirectory(data_sources)

# 内置业务组件：只在这里聚合，base 不认识具体组件和格式。
# 视频同步组件暂停：dock.video 与 ffmpeg 抽帧链路先不参与构建（后续作为独立模块重做）
# add_subdirectory(video)
add_subdirectory(device_debug)
add_subdirectory(script)
add_subdirectory(autopilot)
add_subdirectory(automotive)
add_subdirectory(robotics)

if(PS_BUILD_GNSS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gnss/CMakeLists.txt)
  add_subdirectory(gnss)
endif()

# 聚合 target：base 只链接它；为空时不影响底座构建。
# 组件对象通过 INTERFACE_SOURCES 进入宿主，保证静态注册器不被裁掉。
add_library(ps_builtin_components INTERFACE)
target_sources(ps_builtin_components INTERFACE
  $<TARGET_OBJECTS:ps_builtin_data_sources_component>
  $<TARGET_OBJECTS:ps_builtin_device_debug_component>
  $<TARGET_OBJECTS:ps_builtin_script_component>
  $<TARGET_OBJECTS:ps_autopilot_component>
  $<TARGET_OBJECTS:ps_automotive_component>
  $<TARGET_OBJECTS:ps_robotics_component>
)
target_link_libraries(ps_builtin_components INTERFACE
  ps_builtin_data_sources_component
  ps_builtin_device_debug_component
  ps_builtin_script_component
  ps_autopilot_component
  ps_autopilot
  ps_automotive_component
  ps_robotics_component
)
if(TARGET ps_gnss_component)
  target_sources(ps_builtin_components INTERFACE $<TARGET_OBJECTS:ps_gnss_component>)
  target_link_libraries(ps_builtin_components INTERFACE ps_gnss_component)
endif()
