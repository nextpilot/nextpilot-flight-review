find_package(Qt6 REQUIRED COMPONENTS Core)

# CLI / headless 可执行文件：只依赖 Core，不链接 Qt Widgets。
# 保证无显示环境下也能跑（CI、服务器、批处理）。
add_executable(plotscope-ng
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
target_link_libraries(plotscope-ng PRIVATE
    ng_module_data ng_module_plot ng_module_script ng_module_protocol ng_module_analysis ng_module_settings ng_module_report
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_context ng_recording Qt6::Core)
set_target_properties(plotscope-ng PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ng_deploy_windows_runtime_all()

