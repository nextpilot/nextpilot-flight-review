add_library(ps_builtin_transform_component OBJECT
  TransformComponent.cpp
  TransformComponent.h
  TransformDialog.cpp
  TransformDialog.h
)
set_target_properties(ps_builtin_transform_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_transform_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/modules/data_source/include
)
target_link_libraries(ps_builtin_transform_component PUBLIC ps_host ps_kernel Qt6::Widgets)
