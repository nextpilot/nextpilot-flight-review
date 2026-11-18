# fastplot —— 独立绘图库（只依赖 Qt Widgets，不依赖 ng/core）
#
# 设计见 docs/roadmap/fastplot.md。要点：
#   - 零拷贝：曲线只持数据指针，不复制（P2 起）
#   - 像素列聚合：按屏幕像素列取 min/max，不逐点描线（P2 起）
#   - 图层缓存：网格/轴画进 QPixmap，视口变化才重画
#
# 只允许 GUI 侧（ng_gui / GUI 测试）链接；CLI（plotscope-ng）不链。

add_library(fastplot STATIC
    src/Viewport.cpp
    src/ScaleDiv.cpp
    src/Axis.cpp
    src/PlotWidget.cpp
    # 头文件必须列进 sources，AUTOMOC 才会为带 Q_OBJECT 的类生成 moc。
    include/fastplot/Viewport.h
    include/fastplot/ScaleDiv.h
    include/fastplot/Axis.h
    include/fastplot/PlotWidget.h
)

target_include_directories(fastplot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(fastplot PUBLIC Qt6::Widgets Qt6::Gui Qt6::Core)

set_target_properties(fastplot PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON
)
