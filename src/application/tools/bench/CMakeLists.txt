# GUI 百万行实测基准（手动跑，不进 ctest）：验证大数据量下渲染路径的内存与速度。
# 装配层目标，保留完整模块 / 工具箱 / 解析器链接；壳目录里不再有这份源码。
add_executable(plotscope-gui-bench bench_gui.cpp)
target_link_libraries(plotscope-gui-bench PRIVATE
    ps_app
    ps_gui
    ps_gui_theme
    ps_gui_transform
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_toolbox_automotive_gui
    ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-gui-bench PROPERTIES AUTOMOC ON CXX_STANDARD 17)
