add_library(ps_builtin_transform_component OBJECT
  TransformComponent.cpp
  TransformComponent.h
  TransformDialog.cpp
  TransformDialog.h
)
set_target_properties(ps_builtin_transform_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_transform_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/datasource/include/datasource
)
target_link_libraries(ps_builtin_transform_component PUBLIC ps_host ps_kernel ps_application_shell Qt6::Widgets)
