#pragma once

#include "AppContext.h"
#include "Transform.h"
#include "IIntentExecutor.h"
#include "AxesView.h"

#include <QObject>
#include <QTimer>
#include <QString>
#include <QColor>
#include <functional>

namespace nextpilot::plotscope::gui
{

// 通道视图：把 N 条通道叠加呈现到画布上。
//
// 它是**绘图视图的一部分**，不是外壳的一部分——外壳（MainWindow）不认识
// 通道、不认识数据集、不发渲染意图。这里只做一件事：
//
//   通道加入/视口变化 → 逐条走意图层取像素级数据 → 填进画布。
//
// 走意图层（而不是直接调 Dataset）带来两个好处：
//   1. GUI 与 CLI/脚本**同一条路径**，行为一致、可录制回放；
//   2. 视图不认识数据层——换掉渲染实现（Rust LOD / 别的来源）不影响它。
//
// 正常使用 plot 的核心：**多条曲线叠加**。每条通道独立取数、独立颜色，
// 双击/拖拽字段树是"追加曲线"（同通道去重），不再互相覆盖。
// 内存铁律不变：每条的像素级三元组数量 <= 画布像素宽，与数据行数无关。
class ChannelView : public QObject
{
    Q_OBJECT
public:
    ChannelView(const nextpilot::plotscope::uischema::TransformHub &transforms,
                nextpilot::plotscope::intent::IIntentExecutor &executor, AxesView *canvas,
                QObject *parent = nullptr);

    // 追加一条通道并渲染（同 dataset+table+column 已存在则只刷新，不重复叠加）。
    // 返回 false 表示该通道取数失败（err 里说明原因）。
    // title 非空时作为曲线显示名（叠加/对比要带源前缀，如「源A:attitude.pitch」）；
    // 空则沿用默认「datasetId / table.column」。
    // color 非空时作为曲线显式颜色（恢复链读回存盘 color 用），空则走 palette 分配。
    bool addChannel(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table, const QString &column,
                    QString *err = nullptr, const QString &title = QString(), const QColor &color = QColor());
    // 兼容旧名：等价于 addChannel（"显示某通道"=叠加它）。
    bool showChannel(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table, const QString &column,
                     QString *err = nullptr);

    // 移除/清空曲线。
    bool removeChannel(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table, const QString &column);
    void clearChannels();

    // 当前叠加的通道数。
    int channelCount() const;

    // 数据集替换后的曲线重绑（对标 PJ4 rebindCurveKeys）：resolve(oldId, table,
    // column) 返回新 datasetId（<0 = 无处可绑）。旧 id 的曲线改写到新 id 并重取
    // 数据；显式颜色 / Y 变换配方等曲线属性原样保留（kept 语义）。返回重绑条数。
    // 找不到新归属的曲线保持原样留在画布（数据取不到即空显），不删除。
    int rebindDataset(const std::function<int(nextpilot::plotscope::dataset::DatasetId,
                                              const QString &, const QString &)> &resolve);

    AxesView *canvas() const { return m_canvas; }

private:
    const nextpilot::plotscope::uischema::TransformHub &m_transforms;
    void refresh();
    // 取一条通道的数据并写入画布的一条 PlotSeries。
    bool renderOne(PlotSeries &series, QString *err);
    // 重取全部通道（视口变化时用）。
    bool renderAll(QString *err);
    // 曲线的稳定键。
    static QString makeKey(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table,
                           const QString &column);

    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    AxesView *m_canvas = nullptr;
    QTimer m_refreshTimer;

    // 所有叠加通道（含各自的 LOD/颜色；与画布 series 一一对应）。
    QVector<PlotSeries> m_channels;

    // 缩放后的可见时间窗（秒）。空 = 全量。
    bool m_windowSet = false;
    double m_winT0 = 0.0;
    double m_winT1 = 0.0;
};

} // namespace nextpilot::plotscope::gui
