#pragma once

#include "PlotCanvas.h"

#include <QObject>
#include <QTimer>
#include <QString>

namespace ng::core
{
class AppContext;
class IIntentExecutor;
} // namespace ng::core

namespace ng::gui
{

// 通道视图：把「一个通道」呈现到画布上。
//
// 它是**绘图视图的一部分**，不是外壳的一部分——外壳（MainWindow）不认识
// 通道、不认识数据集、不发渲染意图。这里只做一件事：
//
//   活动通道变化 → 走意图层取像素级数据 → 填进画布。
//
// 走意图层（而不是直接调 Dataset）带来两个好处：
//   1. GUI 与 CLI/脚本**同一条路径**，行为一致、可录制回放；
//   2. 视图不认识数据层——换掉渲染实现（Rust LOD / 别的来源）不影响它。
//
// 内存铁律不变：像素级三元组数量 <= 画布像素宽，与数据行数无关。
//
// **分辨率也由这里保证**：LOD 的桶数必须等于画布的实际像素宽，
// 否则窄屏取的 LOD 会被拉到大屏（梳齿状假象）。所以视图订阅画布的
// viewportChanged，视口一变就按新宽度重新取数。
class ChannelView : public QObject
{
    Q_OBJECT
public:
    ChannelView(ng::core::IIntentExecutor &executor, PlotCanvas *canvas,
                QObject *parent = nullptr);

    // 呈现指定通道。返回 false 表示取数失败（err 里说明原因）。
    bool showChannel(ng::core::DatasetId datasetId, const QString &table, const QString &column,
                     QString *err = nullptr);

    PlotCanvas *canvas() const { return m_canvas; }

private:
    // 按当前画布像素宽重新取数（视口变化时调用）。
    void refresh();

    ng::core::IIntentExecutor &m_executor;
    PlotCanvas *m_canvas = nullptr;

    // 视口变化的合并重取定时器（拖动窗口时避免连发重扫）。
    QTimer m_refreshTimer;

    // 当前呈现的通道（视口变化时按它重新取数）。
    ng::core::DatasetId m_datasetId = ng::core::kInvalidDataset;
    QString m_table;
    QString m_column;
};

// 绘图视图的工厂：创建画布 + 通道视图，并让视图订阅共享的"活动通道"。
//
// 关键：画布**自己订阅** AppContext 的活动通道变化——
// 于是数据集树双击 → 画布自动重绘，中间没有任何直接调用。
// 绘图视图与数据集视图互不认识。
QWidget *createPlotCanvas(ng::core::AppContext &appCtx, ng::core::IIntentExecutor &executor,
                          QWidget *parent);

} // namespace ng::gui
