find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# GUI 层：**外壳 + 功能视图**两层，界面按功能分组。
#
# 架构定位（见 ARCHITECTURE.md §7）：
#   外壳层不设 "DockManager" / "RibbonManager" 这类中转类——ADS 与 SARibbon 已提供
#   对应能力，再包一层同名类只是零价值转发。ng 只保留**一个绑定器**：
#
#   shell/                       外壳层：只做装配，不认识任何业务
#     MainWindow                  窗口骨架与装配
#     ShellBinder                 唯一知道 SARibbon / ADS 的地方
#     GuiContext                  host::UiContext 的实现
#   （部件工厂表 PanelFactoryRegistry 已下沉到 core —— 它是纯数据，
#    知识包的呈现层要能注册领域面板，就不能依赖整个 gui 层）
#
#   views/                       通用绘图能力库（将来抽成 Qwt-like 独立库）
#     plot/                       绘图画布 + 通道视图，不属于任何器官/领域
#
# 功能**专属**视图已聚合进对应 capabilities 目录（如 data 的树视图在
# modules/data/DataManagerPanel.*），与器官同目录、靠 CMake 拆 headless/GUI
# 两个 target。判据：只服务一个功能的专属 UI → 聚合进该功能目录；
# 通用能力库（如绘图）→ 留 gui/views/；形态随领域变 → extensions/<域>/gui/。
#
# 关键约束：
#   - **外壳不认识视图**。MainWindow 不 include 任何 views/ 下的头文件，
#     视图之间的协作走 AppContext 的共享状态（星形而非网状）。
#   - **一功能一目录**。加一个新功能 = 新建一个 views/<功能>/ 目录，
#     外壳与其它功能的文件一律不动。
#
# OBJECT 库：承载视图层的自注册 TU（NG_REGISTER_PANEL）。
# 静态库中未被引用的目标文件会被链接器丢弃、静态初始化不执行，
# 自注册会**静默失效**（部件工厂表为空，requestPanel 拿不到东西）。
add_library(ng_gui OBJECT
    # --- 外壳层 ---
    shell/MainWindow.cpp
    shell/MainWindow.h
    shell/ShellBinder.cpp
    shell/ShellBinder.h
    shell/GuiContext.cpp
    shell/GuiContext.h
    # --- 通用绘图能力库 ---
    # 数据集视图已聚合进 modules/data/（功能专属，见该目录 CMakeLists）；
    # 这里只剩**通用绘图库**（将来抽成 Qwt-like 独立库，不属于任何器官/领域）：
    # PlotModule 用它，bench/shot 工具也直接用它。
    views/plot/PlotCanvas.cpp
    views/plot/PlotCanvas.h
    views/plot/ChannelView.cpp
    views/plot/ChannelView.h
    # --- 资源 ---
    resources/icons.qrc
)
target_include_directories(ng_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/views/plot)
# fastplot：独立绘图库（只依赖 Qt Widgets）。绘图视图通过它绘制，
# 因此 GUI 侧公共依赖；CLI 不链它，保持 headless 纯净。
target_link_libraries(ng_gui PUBLIC ng_host ng_core fastplot Qt6::Widgets Qt6::Gui Qt6::Core
                                    ads::ads SARibbonBar::SARibbonBar)
set_target_properties(ng_gui PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 17)

# 图形界面可执行文件：与 CLI 并列，共用同一套 app/host/core。
add_executable(plotscope-ng-gui
    gui_main.cpp
    ${CMAKE_SOURCE_DIR}/app/App.cpp
    ${CMAKE_SOURCE_DIR}/app/App.h
)
target_include_directories(plotscope-ng-gui PRIVATE ${CMAKE_SOURCE_DIR}/app)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册。
target_link_libraries(plotscope-ng-gui PRIVATE
    ng_gui
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    # 领域面板的**呈现层**：只 GUI 链接，CLI 不含（保持 headless 纯净）。
    ng_ext_automotive_gui
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-ng-gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# GUI 百万行实测（手动基准，不进 ctest）：验证大数据量下渲染路径的内存与速度。
add_executable(plotscope-ng-gui-bench
    bench_gui.cpp
    ${CMAKE_SOURCE_DIR}/app/App.cpp
    ${CMAKE_SOURCE_DIR}/app/App.h
)
target_include_directories(plotscope-ng-gui-bench PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(plotscope-ng-gui-bench PRIVATE
    ng_gui
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_automotive_gui
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-ng-gui-bench PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# GUI 截图工具（手动运行）：加载真实数据 → 渲染界面 → 存 PNG。
# 用途是让人直接看到界面，而不是靠描述猜。
add_executable(plotscope-ng-gui-shot
    shot_gui.cpp
    ${CMAKE_SOURCE_DIR}/app/App.cpp
    ${CMAKE_SOURCE_DIR}/app/App.h
)
target_include_directories(plotscope-ng-gui-shot PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(plotscope-ng-gui-shot PRIVATE
    ng_gui
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_automotive_gui
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-ng-gui-shot PROPERTIES AUTOMOC ON CXX_STANDARD 17)
