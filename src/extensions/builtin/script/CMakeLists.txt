# 临时过渡：只剩一个 ScriptComponent，把旧 runner 槽接进脚本模块的引擎。
# 等绘图侧摘掉曲线变换里的 lua 项后，本目录连同 runner 槽整体删除（见方案 §8 S0.5）。
add_library(ps_builtin_script_component OBJECT
  ScriptComponent.cpp
  ScriptComponent.h
)
set_target_properties(ps_builtin_script_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_script_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/shell
)
target_link_libraries(ps_builtin_script_component PUBLIC
  ps_host
  ps_application_scripting
  Qt6::Widgets
)
