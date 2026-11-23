#pragma once

#include "Dataset.h"

#include <QColor>
#include <QVector>

namespace nextpilot::plotscope::gui
{

// 画布上的一条曲线：逻辑标识 + 有界 LOD + 样式。
// 数据与样式都属于曲线自身，与 AxesView 的界面状态无关。
struct PlotSeries
{
    nextpilot::plotscope::dataset::DatasetId datasetId = nextpilot::plotscope::dataset::kInvalidDataset;
    QString table;
    QString column;
    QString title;             // 完整标题（数据集 / table.column）
    QColor color;              // 曲线色（由上层按主题分配）
    QVector<nextpilot::plotscope::dataset::LodTriple> lod;
    double t0 = 0.0;
    double t1 = 0.0;
    bool visible = true;
    bool rightAxis = false;    // 挂右 y 轴
    qreal lineWidth = 1.4;

    // 曲线内变换（X 轴时间 offset，Y 轴算子/表达式/Lua 的配方）。
    double xTimeOffset = 0.0;
    QString yTransformId;
    QString yTransformParamsJson;

    // 稳定键：同一通道重复拖入时据此去重（不重复叠加）。
    QString key() const
    {
        return QStringLiteral("%1|%2|%3").arg(datasetId).arg(table, column);
    }
    QString legendName() const
    {
        return table.isEmpty() ? column : QStringLiteral("%1.%2").arg(table, column);
    }
};

} // namespace nextpilot::plotscope::gui
