# 组合根（装配层）：完整产品的可执行在这里装配。
# 壳（ps_gui）只链 libs 层；模块/工具箱/解析器的链接清单属于**装配**，不属于壳，
# 因此本文件是仓库里唯一允许出现 ps_module_* / ps_toolbox_* / ps_parser_* 的 gui 侧文件。
# （壳目录 src/application/gui 的 grep 必须为 0，见契约 §11 链接关 / 验收 6。）

add_executable(plotscope-gui "${CMAKE_SOURCE_DIR}/src/application/gui/gui_main.cpp")
target_link_libraries(plotscope-gui PRIVATE
    ps_app
    ps_gui
    # 主题库必须**显式链到可执行文件**：OBJECT 库链 OBJECT 库只传播 usage
    # requirements，不传播目标文件——不显式链则 ThemeManager 的实现不会进二进制。
    ps_gui_theme
    ps_gui_transform
    # 装配清单：模块 + 工具箱 + 解析器（产品的"业务"全部来自这里）。
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    # 领域面板的呈现层：只 GUI 链接，CLI 不含（保持 headless 纯净）。
    ps_toolbox_automotive_gui
    ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)

ps_deploy_windows_runtime_all()
