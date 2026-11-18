find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# GUI 层：只放"界面骨架与部件"，不含业务、不含数据访问。
#
# 文件划分原则（对应"modules 是 app 的组成部分，避免 MainWindow 变大"）：
#   MainWindow        窗口骨架与装配，零业务
#   RibbonBar         ribbon 控件（页签/分组/按钮）
#   DockManager       四边停靠管理
#   PlotCanvas        中央画布（QPainter 绘制 Rust LOD 三元组）
#   DataManagerPanel  数据集->表->列 树
#   GuiContext        host::UiContext 的 GUI 实现
#
# 每个部件一个独立文件/类：出问题时 grep 一个文件即可定位。
add_library(ng_gui STATIC
    MainWindow.cpp
    MainWindow.h
    RibbonBar.cpp
    RibbonBar.h
    DockManager.cpp
    DockManager.h
    PlotCanvas.cpp
    PlotCanvas.h
    DataManagerPanel.cpp
    DataManagerPanel.h
    GuiContext.cpp
    GuiContext.h
    PanelFactoryRegistry.cpp
    PanelFactoryRegistry.h
)
target_include_directories(ng_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_gui PUBLIC ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
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
