# 裸壳独立产物 ps_shell_bare（契约 §19/§20，验收 6/18/19/20）。
#
# 链接面只许：ps_gui + libs 层 + Qt Widgets。一个 ps_module_* / ps_toolbox_* /
# ps_parser_* 都不许出现——护栏 ps_guard_shell_links 与 ps_gui 共用同一道，
# configure 期硬拦。本目标与完整产品 plotscope-gui 在同一 build 目录共存。

option(PS_BUILD_BARE "构建裸壳独立产物 ps_shell_bare（零模块第二产品）" ON)

if(PS_BUILD_BARE)
    add_executable(ps_shell_bare bare_main.cpp)

    target_link_libraries(ps_shell_bare PRIVATE
        ps_gui
        ps_gui_theme
        ps_gui_transform
        ps_host
        ps_context
        ps_command
        ps_protocol
        ps_registry
        Qt6::Widgets Qt6::Gui Qt6::Core)
    set_target_properties(ps_shell_bare PROPERTIES AUTOMOC ON CXX_STANDARD 17)

    include(${CMAKE_SOURCE_DIR}/cmake/ShellLinkGuard.cmake)
    ps_guard_shell_links(ps_shell_bare)
    # 验收 20：闭包层面也断言一次（直接面已由 ps_guard_shell_links 拦住，
    # 这里防 libs 层某个 INTERFACE 把 ps_module_* 传递进来）。
    ps_guard_closure_has_no_business(ps_shell_bare)

    ps_deploy_windows_runtime(ps_shell_bare)
endif()
