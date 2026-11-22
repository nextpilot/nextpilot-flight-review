#pragma once

#include <QIcon>

// 图库缩略图：按 visual 种类画一张小图，用于 ribbon gallery 的图型/格式项。
//
// 这些绘制逻辑抄自老 PlottingRibbon::plotThumb 的 UI 体验（12 种图型一眼可辨），
// 但只依赖 Qt 的 QPainter 与 ThemeManager 配色——不依赖老 CommandManager / 胖 widget，
// 因而是纯展示工具，放在 GUI 层，core 不认识它。
//
// 配色走 ThemeManager::adaptColor，深浅主题都可读。
class PlotThumbnails
{
public:
    // kind: timeseries/xy/scatter/bar/histogram/fft/traj3d/map/surface3d/horizon/compass/gauge
    //       以及文件格式 csv/ulog/custom-protocol/... 与实时源 stream。
    static QIcon plot(const QString &kind);

    // 布局缩略图（"布局"组的大按钮）：按 rows×cols 画分格，抄 main 的 layoutThumb。
    // 与图型缩略图同一尺寸与配色，视觉上属于同一套 ribbon 缩略图。
    static QIcon layout(int rows, int cols);
};
