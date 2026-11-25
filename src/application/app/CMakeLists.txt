find_package(Qt6 REQUIRED COMPONENTS Core)

# L6 组合根（app）：一个共享核心 `ps_app` + 两个入口面（headless / GUI）。
#
# 目录职责（每个子目录一个关注点，物理聚合在此层）：
#
#   core/  App.h/.cpp          —— 组合根本体：模块装配、运行时启动、生命周期
#   cli/   cli_main.cpp        —— 一次性批处理命令行入口（headless）
#   mcp/   mcp_bridge.h/.cpp   —— MCP stdio 桥（JSON-RPC 协议层）
#   diag/  diagnostics.h/.cpp  —— 诊断快照（注册树/意图统计）
#
# 与 `application/gui/shell/`（L5 外壳装配）不同名不同义：
#   app/     = L6，进程级组合根
#   gui/shell/ = L5，窗口外壳装配
#
# `ps_app`（L3）：组装根三件套收成一份源码，headless 与 GUI 共用（#IKXGYH c1b）。
#   * 不链任何 ps_module_* / ps_toolbox_*：模块是装配物，不是核心自带（契约 §11 链接关）。
#   * 不链 Qt Widgets：保证 headless 纯度。
#   * 用 STATIC 而非 OBJECT：AGENTS.md 硬线 #6 针对的是"自注册 TU"（模块/知识包靠静态对象
#     注册）。ps_app 内没有自注册 TU，App / MCP / 诊断的符号都被入口显式引用，静态库不会丢。
#     往本库加自注册型 TU 之前，先看那条硬线。

add_library(ps_app STATIC
    core/App.cpp core/App.h
    mcp/mcp_bridge.cpp mcp/mcp_bridge.h
    diag/diagnostics.cpp diag/diagnostics.h
)
target_include_directories(ps_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp
    ${CMAKE_CURRENT_SOURCE_DIR}/diag)
target_link_libraries(ps_app PUBLIC
    ps_host ps_context ps_intent ps_recording Qt6::Core)
set_target_properties(ps_app PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 最小 app 断言（契约 §11 链接关 / 验收 20）：ps_app 的**传递闭包**里不许出现业务模块。
# 一旦被 PUBLIC 链住，"换个产品只换 modules 集合"就不成立了。
include(${CMAKE_SOURCE_DIR}/cmake/ShellLinkGuard.cmake)
ps_guard_closure_has_no_business(ps_app)

# --- cli：一次性批处理入口（只依赖 Core，不链接 Qt Widgets） ---
# 保证无显示环境下也能跑（CI、服务器、批处理）。
add_executable(plotscope
    cli/cli_main.cpp
)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册，
# 链接器不参与的 TU 不会执行其静态初始化。
target_link_libraries(plotscope PRIVATE
    ps_app
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_host ps_context ps_recording Qt6::Core)
set_target_properties(plotscope PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ps_deploy_windows_runtime_all()
