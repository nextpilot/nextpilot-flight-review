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
#     PanelFactoryRegistry        部件类型 -> QWidget 工厂
#
#   views/<功能>/                功能视图层：一个功能一个目录
#     dataset/                    数据集管理（数据集->表->列 树）
#     plot/                       绘图（画布 + 通道视图）
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
    shell/PanelFactoryRegistry.cpp
    shell/PanelFactoryRegistry.h
    # --- 功能视图层 ---
    views/dataset/DataManagerPanel.cpp
    views/dataset/DataManagerPanel.h
    views/plot/PlotCanvas.cpp
    views/plot/PlotCanvas.h
    views/plot/ChannelView.cpp
    views/plot/ChannelView.h
)
target_include_directories(ng_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/views/dataset
    ${CMAKE_CURRENT_SOURCE_DIR}/views/plot)
target_link_libraries(ng_gui PUBLIC ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core
                                    ads::ads SARibbonBar::SARibbonBar)
set_target_properties(ng_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)

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
    ng_module_data ng_module_plot ng_module_script
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
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
    ng_module_data ng_module_plot ng_module_script
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-ng-gui-bench PROPERTIES AUTOMOC ON CXX_STANDARD 17)
