# ps_command：命令 / 键位 / when 求值（契约库）。
#
# 纯数据 + 纯函数，无 Widgets、无上层依赖，headless 与 GUI 共用同一套命令语义。
# 权威清单只在这里；Ribbon 组、命令面板、上下文菜单、键位、视图标题栏都只是挂载点。
add_library(ps_command STATIC
    src/CommandRegistry.cpp
    src/ContextStack.cpp
    src/WhenExpression.cpp
    src/KeybindingRegistry.cpp
    include/command/CommandRegistry.h
    include/command/ContextStack.h
    include/command/WhenExpression.h
    include/command/WhenKeys.h
    include/command/KeybindingRegistry.h
)
target_include_directories(ps_command PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# QKeySequence（键位规范化）属 QtGui；不是 Widgets，headless 亦可链。
target_link_libraries(ps_command PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(ps_command PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
