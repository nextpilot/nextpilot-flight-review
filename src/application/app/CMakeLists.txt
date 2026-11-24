find_package(Qt6 REQUIRED COMPONENTS Core)

# L6 组合根（app）：App 装配 + 三个入口面。
#
# 目录职责（每个子目录一个关注点，物理聚合在此层）：
#
#   core/  App.h/.cpp          —— 组合根本体：模块装配、运行时启动、生命周期
#   cli/   cli_main.cpp        —— 一次性批处理命令行入口
#   mcp/   mcp_bridge.h/.cpp   —— MCP stdio 桥（JSON-RPC 协议层）
#   diag/  diagnostics.h/.cpp  —— 诊断快照（注册树/意图统计）
#
# 与 `application/gui/shell/`（L5 外壳装配）不同名不同义：
#   app/     = L6，进程级组合根
#   gui/shell/ = L5，窗口外壳装配
#
# 为什么 CLI 与 GUI 各自编译 App.cpp（而非共用一个库）：
#   见 #IKXG0M PR2 / #IKXGYH c1b —— 抽 ps_app 静态库消除 13 处重复编译。
#   本 PR 是纯路径搬迁，不改构建拓扑。

# --- core：组合根本体 ---
# OBJECT 库：模块/知识包靠 TU 静态对象自注册，静态库会丢弃未被引用的
# 目标文件导致注册静默失效（见 AGENTS.md 硬线 #6）。
add_library(ps_app_core OBJECT
    core/App.cpp
    core/App.h
)
target_include_directories(ps_app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(ps_app_core PUBLIC ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_app_core PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# --- mcp：MCP stdio 桥（协议层，纯 QtCore） ---
add_library(ps_app_mcp OBJECT
    mcp/mcp_bridge.cpp
    mcp/mcp_bridge.h
)
target_include_directories(ps_app_mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mcp)
target_link_libraries(ps_app_mcp PUBLIC ps_app_core ps_intent Qt6::Core)
set_target_properties(ps_app_mcp PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# --- diag：诊断快照 ---
add_library(ps_app_diag OBJECT
    diag/diagnostics.cpp
    diag/diagnostics.h
)
target_include_directories(ps_app_diag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/diag)
target_link_libraries(ps_app_diag PUBLIC ps_app_core ps_host Qt6::Core)
set_target_properties(ps_app_diag PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# --- cli：一次性批处理入口（只依赖 Core，不链接 Qt Widgets） ---
# 保证无显示环境下也能跑（CI、服务器、批处理）。
add_executable(plotscope
    cli/cli_main.cpp
)
target_include_directories(plotscope PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp
    ${CMAKE_CURRENT_SOURCE_DIR}/diag)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册，
# 链接器不参与的 TU 不会执行其静态初始化。
target_link_libraries(plotscope PRIVATE
    ps_app_core ps_app_mcp ps_app_diag
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_host ps_context ps_recording Qt6::Core)
set_target_properties(plotscope PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ps_deploy_windows_runtime_all()
