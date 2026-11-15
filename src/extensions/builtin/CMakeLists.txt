# 内置业务组件：只在这里聚合，base 不认识具体组件和格式。
add_subdirectory(data_sources)

if(PS_BUILD_CUSTOM_PROTOCOL AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/custom_protocol/CMakeLists.txt)
  add_subdirectory(custom_protocol/protocol)
  add_subdirectory(custom_protocol)
endif()

if(PS_BUILD_GNSS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gnss/CMakeLists.txt)
  add_subdirectory(gnss)
endif()

# 聚合 target：base 只链接它；为空时不影响底座构建。
# 组件对象通过 INTERFACE_SOURCES 进入宿主，保证静态注册器不被裁掉。
add_library(ps_builtin_components INTERFACE)
target_sources(ps_builtin_components INTERFACE
  $<TARGET_OBJECTS:ps_builtin_data_sources_component>
)
target_link_libraries(ps_builtin_components INTERFACE
  ps_builtin_data_sources_component
)
if(TARGET ps_custom_protocol_component)
  target_sources(ps_builtin_components INTERFACE $<TARGET_OBJECTS:ps_custom_protocol_component>)
  target_link_libraries(ps_builtin_components INTERFACE ps_custom_protocol_component)
endif()
if(TARGET ps_gnss_component)
  target_sources(ps_builtin_components INTERFACE $<TARGET_OBJECTS:ps_gnss_component>)
  target_link_libraries(ps_builtin_components INTERFACE ps_gnss_component)
endif()
