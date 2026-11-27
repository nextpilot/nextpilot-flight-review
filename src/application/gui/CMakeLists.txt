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
#   （视图/编辑器声明的注册机制在 core 的 ViewRegistry —— 纯数据，
#    知识包的呈现层要能注册领域面板，就不能依赖整个 gui 层）
#
# 绘图视图（原 views/plot）已下沉到绘图模块的 gui 呈现层（c4）；
# PropertyPanel 也已归位 modules/plot/gui（A2c，owner "plot" 自声明自注册）；
# 壳自有面板只剩 diagnostics / output / problems / debugconsole。
#
# 关键约束：
#   - **外壳不认识视图**。MainWindow 不 include 任何业务视图头文件，
#     视图之间的协作走 AppContext 的共享状态（星形而非网状）。
#
# OBJECT 库：承载视图层的自注册 TU（views/editor 声明 + provider 注册，同 TU 同次完成）。
# 静态库中未被引用的目标文件会被链接器丢弃、静态初始化不执行，
# 自注册会**静默失效**（声明进不了 ViewRegistry，openView 拿不到东西）。
# 主题：横向 GUI 基础设施（token 化配色 + 双主题 + QSS/palette）。
#
# 独立 OBJECT 库，而非并进 ps_gui：主题是**被依赖的服务**，不只外壳用——
# 功能专属视图（如数据源面板的状态圆点）也要从它取色。若把它塞进 ps_gui，
# 那些视图就得链接整个外壳库（且 OBJECT 库重复链接会撞符号）。
# 拆出来后：谁需要谁链 ps_gui_theme，依赖关系是显式的。
add_library(ps_gui_theme OBJECT
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    theme/IconRegistry.cpp
    theme/IconRegistry.h
    theme/MenuIcons.cpp
    theme/MenuIcons.h
)
target_include_directories(ps_gui_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/theme)
# ps_uischema：纯 token 色表 / 取色函数在 libs/uischema（ThemeTokens），本库只留
# 状态、QSS/palette 生成与图标/缩略图渲染。
target_link_libraries(ps_gui_theme PUBLIC ps_context ps_uischema Qt6::Gui Qt6::Widgets Qt6::Core)
set_target_properties(ps_gui_theme PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 变换编辑器 + 在线调试对话框已迁至 libs/widget（ps_widgets）：
# 这些是被 data / plot / protocol 多模块直接复用的通用控件，物理归属与 target 名一并纠正。
# 外壳只按能力回调使用，不编译任何控件源文件，也不 include 控件头。

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
    # 壳内置工作台视图装配·注册半（复审必修 2）：registerViewProvider 在 gui 侧，
    # 与 assembly 的声明半（BuiltinWorkbenchViews）分立，装配层次不倒挂。
    shell/BuiltinViewProviders.cpp
    shell/BuiltinViewProviders.h
    shell/CommandPalette.cpp
    shell/CommandPalette.h
    shell/KeybindingDialog.cpp
    shell/KeybindingDialog.h
    # 导入对话框已迁至 modules/data/gui（D18a）：外壳按 id 经装配注入的启动器回调触发。
    # 协议编辑器/检查器对话框已迁至 modules/protocol/gui（D18a）：
    # 外壳零业务对话框，只通过意图触发；ps_gui 不再编译任何协议 UI。
    # 设置入口（preferences.open）：只转派到工作台内置 Settings editor，不提供界面。
    shell/PreferencesIntentHandler.cpp
    shell/PreferencesIntentHandler.h
    # 报告导出已迁至 modules/report/gui（D18a）：装配层注册 viewAction，外壳零业务 include。
    # PropertyPanel 已归位 modules/plot/gui（A2c：业务视图模块自声明自注册），
    # gui/views 下只剩壳自有的诊断/输出/问题/调试控制台。
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
    ${CMAKE_CURRENT_SOURCE_DIR}/theme
    # 注册半（BuiltinViewProviders）需要 gui/views 各面板的导出头（复审必修 2：
    # 工厂实现留 gui/views，ps_gui 自己 include 自己的子目录——ps_assembly 不沾）。
    ${CMAKE_CURRENT_SOURCE_DIR}/views/output
    ${CMAKE_CURRENT_SOURCE_DIR}/views/problems
    ${CMAKE_CURRENT_SOURCE_DIR}/views/debugconsole
    ${CMAKE_CURRENT_SOURCE_DIR}/views/diagnostics)
# c8（契约 §11 链接关 / 验收 6）：ps_gui 的链接依赖**只许**是 libs 层
# （ps_host / ps_context / ps_command / ps_gui_theme / ps_widgets / 其余 libs/*）
# 与 Qt；不许出现任何业务模块、工具箱、解析器库，也不含 fastplot / ps_protocol——
# 绘图在 plot 模块、协议在 protocol 模块，外壳零业务链接（A1a）。
# 装配权上移到组合根（exe / 装配层），壳只接收服务接口（ISlotsService 等，经装配层注册）。
# 下方护栏在 configure 期硬拦。
target_link_libraries(ps_gui PUBLIC ps_host ps_context ps_command ps_gui_theme ps_widgets
                                    ads::ads SARibbonBar::SARibbonBar
                                    Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_gui PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 17)

# 壳链接护栏（契约 §11 链接关 / 验收 6）：函数定义见 cmake/ShellLinkGuard.cmake，
# 同时罩 ps_gui 与裸壳 ps_shell_bare。
include(${CMAKE_SOURCE_DIR}/cmake/ShellLinkGuard.cmake)
ps_guard_shell_links(ps_gui)

# 完整产品的可执行已搬到装配层 src/application/assembly/（c8：壳目录不出现模块清单）。

# 注：GUI 百万行基准 plotscope-gui-bench 已搬到装配层 src/application/tools/bench/
# （c8b），壳目录不再持有引用模块呈现层头文件的源码。

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ps_deploy_windows_runtime_all()
