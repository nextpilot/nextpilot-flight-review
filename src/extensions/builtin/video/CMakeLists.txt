add_library(ps_builtin_video_component OBJECT
  VideoComponent.cpp
  VideoComponent.h
  VideoSyncWidget.cpp
  VideoSyncWidget.h
)
set_target_properties(ps_builtin_video_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_video_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/host/extension
)
target_link_libraries(ps_builtin_video_component PUBLIC ps_host Qt6::Widgets)
