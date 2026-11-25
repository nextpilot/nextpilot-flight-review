find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# plot 功能的**呈现层**：绘图画布 / 通道视图 / 仪表视图等 QWidget 实现。
#
# 为什么与 headless 器官（ps_module_plot）分成两个产物：
#   器官是纯数据与意图协议（headless 纯净），CLI 也要链接；
#   呈现层含 QWidget，只能被 GUI 链接。分开之后 CLI 依赖树里没有 Qt Widgets。
#
# 依赖方向：呈现层 -> core / 横向 GUI 基础设施，**不依赖 ps_gui 外壳**
# （否则模块就要依赖整个外壳层，依赖方向反转，见 D18/D21）。
#
# OBJECT 库：保证 PS_REGISTER_PANEL 的自注册 TU 一定参与链接
# （静态库中未被引用的目标文件会被链接器丢弃，自注册会静默失效）。
add_library(ps_module_plot_gui OBJECT
    AxesView.cpp
    AxesView.h
    ChannelView.cpp
    ChannelView.h
    InstrumentView.cpp
    InstrumentView.h
    PlotWorkspace.cpp
    PlotWorkspace.h
    WorkspaceHistory.cpp
    WorkspaceHistory.h
    GridPickPopup.cpp
    GridPickPopup.h
    PlaybackEngine.cpp
    PlaybackEngine.h
    TimelineWidget.cpp
    TimelineWidget.h
    # --- chrome：格框上的小部件 ---
    chrome/AxesToolbar.cpp
    chrome/AxesToolbar.h
    # --- layout：页内停靠容器、每格面板与页签栏 ---
    layout/TabbedFigure.cpp
    layout/TabbedFigure.h
    layout/PlotLayout.cpp
    layout/PlotLayout.h
    layout/DockedAxes.cpp
    layout/DockedAxes.h
    # --- commands ---
    commands/PageActions.cpp
    commands/CanvasActions.cpp
    commands/SeriesActions.cpp
    commands/MarkupActions.cpp
    commands/PlaybackActions.cpp
    commands/PropertyProviders.cpp
    # --- playback ---
    playback/PlaybackController.cpp
    playback/PlaybackController.h
    # --- axes ---
    axes/PlotSeries.h
    axes/SeriesStore.cpp
    axes/SeriesStore.h
    axes/ContextMenuDecor.cpp
    axes/ContextMenuDecor.h
    axes/CursorReadout.cpp
    axes/CursorReadout.h
    axes/Interaction.cpp
    axes/Interaction.h
    axes/ViewState.cpp
    axes/ViewState.h
)
target_include_directories(ps_module_plot_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # PlotDocument.h 在模块层（上一级目录），视图层按裸名包含。
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 主题（token 配色 / 图标）与变换编辑器是横向 GUI 基础设施，单独成库谁需要谁链；
# fastplot 是绘图库；ads 是停靠系统。不链 ps_gui / ps_shell（模块呈现层禁反向依赖外壳）。
target_link_libraries(ps_module_plot_gui PUBLIC
    ps_context
    fastplot
    ads::ads
    ps_gui_theme
    ps_widgets
    Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_module_plot_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
