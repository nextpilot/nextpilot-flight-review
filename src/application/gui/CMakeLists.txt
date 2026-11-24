find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# GUI 层：**外壳 + 功能视图**两层，界面按功能分组。
#
# 架构定位（见 ARCHITECTURE.md §7）：
#   外壳层不设 "DockManager" / "RibbonManager" 这类中转类——ADS 与 SARibbon 已提供
#   对应能力，再包一层同名类只是零价值转发。GUI 只保留**一个绑定器**：
#
#   shell/                       外壳层：只做装配，不认识任何业务
#     MainWindow                  窗口骨架与装配
#     ShellBinder                 唯一知道 SARibbon / ADS 的地方
#     GuiContext                  host::UiContext 的实现
#   （部件工厂表 PanelFactoryHub 已下沉到 core —— 它是纯数据，
#    知识包的呈现层要能注册领域面板，就不能依赖整个 gui 层）
#
# 绘图视图（原 views/plot）已下沉 modules/plot/gui（ps_module_plot_gui，c4）；
# 通用面板只剩 PropertyPanel / diagnostics / output / problems / debugconsole。
#
# 关键约束：
#   - **外壳不认识视图**。MainWindow 不 include 任何业务视图头文件，
#     视图之间的协作走 AppContext 的共享状态（星形而非网状）。
#
# OBJECT 库：承载视图层的自注册 TU（PS_REGISTER_PANEL）。
# 静态库中未被引用的目标文件会被链接器丢弃、静态初始化不执行，
# 自注册会**静默失效**（部件工厂表为空，requestPanel 拿不到东西）。
# 主题：横向 GUI 基础设施（token 化配色 + 双主题 + QSS/palette）。
#
# 独立 OBJECT 库，而非并进 ps_gui：主题是**被依赖的服务**，不只外壳用——
# 功能专属视图（如数据源面板的状态圆点）也要从它取色。若把它塞进 ps_gui，
# 那些视图就得链接整个外壳库（且 OBJECT 库重复链接会撞符号）。
# 拆出来后：谁需要谁链 ps_gui_theme，依赖关系是显式的。
add_library(ps_gui_theme OBJECT
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    theme/PlotThumbnails.cpp
    theme/PlotThumbnails.h
    theme/MenuIcons.cpp
    theme/MenuIcons.h
)
target_include_directories(ps_gui_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/theme)
# ps_uischema：纯 token 色表 / 取色函数在 libs/uischema（ThemeTokens），本库只留
# 状态、QSS/palette 生成与图标/缩略图渲染。
target_link_libraries(ps_gui_theme PUBLIC ps_context ps_uischema Qt6::Gui Qt6::Widgets Qt6::Core)
set_target_properties(ps_gui_theme PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 变换编辑器 + 在线调试对话框：独立 OBJECT 库，被 gui（曲线/协议）与 data_gui
# （数据源衍生）共用。只依赖 ps_context（AppContext.evalTransform 回调 + TransformHub）
# 与 Qt Widgets，不含任何 script 类，故不会引入层依赖环。
add_library(ps_gui_transform OBJECT
    shell/TransformEditorDialog.cpp
    shell/TransformEditorDialog.h
    shell/FilterEditorDialog.cpp
    shell/FilterEditorDialog.h
    shell/TransformField.cpp
    shell/TransformField.h
    shell/LuaEditor.cpp
    shell/LuaEditor.h
)
target_include_directories(ps_gui_transform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shell)
target_link_libraries(ps_gui_transform PUBLIC ps_context ps_uischema_widgets Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_gui_transform PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

add_library(ps_gui OBJECT
    # --- 外壳层 ---
    shell/MainWindow.cpp
    shell/MainWindow.h
    shell/ShellBinder.cpp
    shell/ShellBinder.h
    shell/ActivityBar.cpp
    shell/ActivityBar.h
    shell/GuiContext.cpp
    shell/GuiContext.h
    shell/CommandPalette.cpp
    shell/CommandPalette.h
    shell/KeybindingDialog.cpp
    shell/KeybindingDialog.h
    shell/ImportDialog.cpp
    shell/ImportDialog.h
    shell/ProtocolInspectorDialog.cpp
    shell/ProtocolInspectorDialog.h
    shell/ProtocolEditorDialog.cpp
    shell/ProtocolEditorDialog.h
    # 全局偏好设置（App 作用域，跨工作区持久）：外壳级系统能力，不属于任何功能模块。
    shell/PreferencesDialog.cpp
    shell/PreferencesDialog.h
    shell/PreferencesIntentHandler.cpp
    shell/PreferencesIntentHandler.h
    shell/ReportExporter.cpp
    shell/ReportExporter.h
    # --- 通用属性面板（dock.right）：PropertySchema 契约的纯视图 ---
    views/PropertyPanel.cpp
    views/PropertyPanel.h
    # --- 诊断面板（dock.diagnostics）：注册树全景，走 diag.registries 取数（M2.8）---
    views/diagnostics/DiagnosticsPanel.cpp
    views/diagnostics/DiagnosticsPanel.h
    # --- 底部页签（dock.bottom，VS Code 语义）：输出 / 问题 / 调试控制台 ---
    # 三者都从 AppContext 的消息流取数据，不再各自维护缓冲。
    views/output/OutputPanel.cpp
    views/output/OutputPanel.h
    views/problems/ProblemsPanel.cpp
    views/problems/ProblemsPanel.h
    # 调试控制台只收 level=debug 的调试事件并复用 script.workbench.repl 求值。
    views/debugconsole/DebugConsolePanel.cpp
    views/debugconsole/DebugConsolePanel.h
    # --- 资源 ---
    resources/icons.qrc
)
target_include_directories(ps_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/theme)
# fastplot：独立绘图库（只依赖 Qt Widgets）。绘图视图通过它绘制，
# 因此 GUI 侧公共依赖；CLI 不链它，保持 headless 纯净。
#
# c8（契约 §11 链接关 / 验收 6）：ps_gui 的链接依赖**只许**是 libs 层
# （ps_host / ps_context / ps_command / ps_gui_theme / ps_gui_transform /
# ps_protocol / 其余 libs/*）、Qt 与 fastplot；不许出现任何
# ps_module_* / ps_toolbox_* / ps_parser_*。装配权上移到组合根（exe / 装配层），
# 壳只接收"已装配好的 Pack + ToolboxHub"。下方护栏在 configure 期硬拦。
target_link_libraries(ps_gui PUBLIC ps_host ps_context ps_command ps_gui_theme ps_gui_transform ps_protocol fastplot
                                    ads::ads SARibbonBar::SARibbonBar
                                    Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_gui PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 17)

# 壳链接护栏（契约 §11 链接关 / 验收 6）：函数定义见 cmake/ShellLinkGuard.cmake，
# 同时罩 ps_gui 与裸壳 ps_shell_bare。
include(${CMAKE_SOURCE_DIR}/cmake/ShellLinkGuard.cmake)
ps_guard_shell_links(ps_gui)

# 图形界面可执行文件：与 headless 入口（src/application/app 的 plotscope）并列，
# App/诊断/MCP 桥来自同一个 ps_app，不再各编一份源码。
add_executable(plotscope-gui gui_main.cpp)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册。
target_link_libraries(plotscope-gui PRIVATE
    ps_app
    ps_gui
    # 主题库必须**显式链到可执行文件**：OBJECT 库链 OBJECT 库只传播
    # usage requirements，不传播目标文件——不显式链则 ThemeManager 的实现
    # 不会进二进制，链接期报 undefined reference（与 ps_gui_theme 的
    # AUTOMOC 产物一起进，故 moc 符号也一并满足）。
    ps_gui_theme
    ps_gui_transform
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    # 领域面板的**呈现层**：只 GUI 链接，CLI 不含（保持 headless 纯净）。
    ps_toolbox_automotive_gui
    ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# 注：GUI 百万行基准 plotscope-gui-bench 已搬到装配层 src/application/tools/bench/
# （c8b），壳目录不再持有引用模块呈现层头文件的源码。

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ps_deploy_windows_runtime_all()
