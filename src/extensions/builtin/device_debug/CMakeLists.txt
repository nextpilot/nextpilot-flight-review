add_library(ps_builtin_device_debug_component OBJECT
  DeviceDebugComponent.cpp
  DeviceDebugComponent.h
  DeviceDebugDialog.cpp
  DeviceDebugDialog.h
)
set_target_properties(ps_builtin_device_debug_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_device_debug_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/host/extension
)
target_link_libraries(ps_builtin_device_debug_component PUBLIC ps_host ps_base_data_source Qt6::Widgets)
