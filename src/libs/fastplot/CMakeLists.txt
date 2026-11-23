# fastplot —— 独立绘图库（只依赖 Qt Widgets，不依赖 ng/core）
#
# 设计要点：
#   - 零拷贝：曲线只持数据指针，不复制（P2 起）
#   - 像素列聚合：按屏幕像素列取 min/max，不逐点描线（P2 起）
#   - 图层缓存：网格/轴画进 QPixmap，视口变化才重画
#
# 只允许 GUI 侧（ps_gui / GUI 测试）链接；CLI（plotscope）不链。

add_library(fastplot STATIC
    src/Viewport.cpp
    src/ScaleDiv.cpp
    src/Axis.cpp
    src/Axes.cpp
    src/Legend.cpp
    src/CursorReadoutOverlay.cpp
    src/ABMeasureOverlay.cpp
    src/BookmarkOverlay.cpp
    src/MarkerOverlay.cpp
    src/StateBandOverlay.cpp
    src/Line.cpp
    src/LodCache.cpp
    src/Zoomer.cpp
    src/Panner.cpp
    src/Picker.cpp
    src/Instrument.cpp
    src/AttitudeIndicator.cpp
    src/CompassRose.cpp
    src/DialGauge.cpp
    src/TrackPlot.cpp
    src/Trajectory3D.cpp
    src/WaterfallPlot.cpp
    src/Bar.cpp
    src/Histogram.cpp
    src/Spectrum.cpp
    src/Figure.cpp
    # 头文件必须列进 sources，AUTOMOC 才会为带 Q_OBJECT 的类生成 moc。
    include/fastplot/Viewport.h
    include/fastplot/ScaleDiv.h
    include/fastplot/Axis.h
    include/fastplot/Axes.h
    include/fastplot/Overlay.h
    include/fastplot/CursorReadoutOverlay.h
    include/fastplot/ABMeasureOverlay.h
    include/fastplot/BookmarkOverlay.h
    include/fastplot/MarkerOverlay.h
    include/fastplot/StateBandOverlay.h
    include/fastplot/Legend.h
    include/fastplot/StateColors.h
    include/fastplot/PlotWidget.h
    include/fastplot/PlotData.h
    include/fastplot/Artist.h
    include/fastplot/Line.h
    include/fastplot/Curve.h
    include/fastplot/LodCache.h
    include/fastplot/Zoomer.h
    include/fastplot/Panner.h
    include/fastplot/Picker.h
    include/fastplot/Instrument.h
    include/fastplot/AttitudeIndicator.h
    include/fastplot/CompassRose.h
    include/fastplot/DialGauge.h
    include/fastplot/TrackPlot.h
    include/fastplot/Trajectory3D.h
    include/fastplot/WaterfallPlot.h
    include/fastplot/Bar.h
    include/fastplot/BarChart.h
    include/fastplot/Histogram.h
    include/fastplot/Spectrum.h
    include/fastplot/SpectrumPlot.h
    include/fastplot/Figure.h
)

target_include_directories(fastplot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(fastplot PUBLIC Qt6::Widgets Qt6::Gui Qt6::Core)

set_target_properties(fastplot PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON
)
