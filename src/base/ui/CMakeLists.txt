# 基础 UI：宿主骨架、通用对话框与基础画布；不含具体业务。
add_library(ps_ui_base STATIC
  shell/Theme.h
  shell/Theme.cpp
  shell/TransformDialogInterface.h
  data_input/DataInputDialog.h
  data_input/DataInputDialog.cpp
  extension_dialog/DialogEngine.h
  extension_dialog/DialogEngine.cpp
  canvas/CanvasWidget.h
  canvas/CanvasWidget.cpp
  script/ScriptService.h
)
set_target_properties(ps_ui_base PROPERTIES AUTOMOC ON)
target_include_directories(ps_ui_base PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/ui
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)
target_link_libraries(ps_ui_base PUBLIC Qt6::Widgets Qt6::Network ps_host ps_kernel)

find_package(Qt6 QUIET COMPONENTS UiTools)
if(Qt6UiTools_FOUND)
  target_link_libraries(ps_ui_base PUBLIC Qt6::UiTools)
  target_compile_definitions(ps_ui_base PUBLIC PS_HAVE_UITOOLS=1)
else()
  target_compile_definitions(ps_ui_base PUBLIC PS_HAVE_UITOOLS=0)
endif()
