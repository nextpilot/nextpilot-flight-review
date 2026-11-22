find_package(Qt6 REQUIRED COMPONENTS Core)

# CLI / headless 可执行文件：只依赖 Core，不链接 Qt Widgets。
# 保证无显示环境下也能跑（CI、服务器、批处理）。
add_executable(plotscope
    cli_main.cpp
    mcp_bridge.cpp
    mcp_bridge.h
    diagnostics.cpp
    diagnostics.h
    App.cpp
    App.h
)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册，
# 链接器不参与的 TU 不会执行其静态初始化。
target_link_libraries(plotscope PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_host ps_context ps_recording Qt6::Core)
set_target_properties(plotscope PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ps_deploy_windows_runtime_all()

