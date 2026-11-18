#pragma once

#include "Dataset.h"

#include <QColor>
#include <QVector>
#include <QWidget>

namespace fastplot
{
class PlotWidget;
}

// 中央绘图画布：把 ng 的 LOD 三元组交给 fastplot 绘制。
//
// 分工（这是 P2 的解耦点）：
//   - 抽稀在 Rust 内核（Dataset::lodMinMax），C++ 只得到 <= 像素宽 个三元组；
//   - 坐标变换、刻度、网格、包络绘制全在 fastplot 里，本类只做"取数 + 适配"。
//
// 内存铁律不变：C++ 侧只持 <= 像素宽 个三元组，与数据总行数无关。
//
// 职责单一：只负责"把一条曲线的 LOD 画出来"，不加载数据、不认识格式、
// 不做交互逻辑（交互由上层转发意图）。
//
// 它**不知道数据从哪来**，所以数据源变化由上层喂进来；
// 但它知道"自己多大"——视口尺寸变化会发 viewportChanged，
// 让上层按新的像素宽度重新取数（否则窄屏取的 LOD 会被拉伸成锯齿）。
namespace ng::gui
{

class PlotCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit PlotCanvas(QWidget *parent = nullptr);
    ~PlotCanvas() override;

    // 设置要绘制的数据集与通道（只存标识，不取数）。
    void setChannel(ng::core::DatasetId datasetId, const QString &table, const QString &column,
                    const QString &title);

    // 直接喂入 LOD 三元组（来自 Dataset::lodMinMax）。
    void setLodData(const QVector<ng::core::LodTriple> &lod, double t0, double t1);

    // 从 Dataset 取 LOD 并绘制（便捷入口，内部按画布像素宽取数）。
    void renderFrom(const ng::core::Dataset &dataset);

    // 清空。
    void clear();

    // 绘图区（不含坐标轴留白）的像素宽度。
    // 上层按这个值向意图层请求 LOD——分辨率决策属于"画布有多大"，
    // 所以由画布自己回答，上层不重复推导。
    int plotPixelWidth() const;

    // 诊断/测试用。
    int lodPointCount() const { return m_lod.size(); }
    QString channelTitle() const { return m_title; }

    // 需要直接操作画布能力（缩放/导出等）时用；正常情况下上层不要碰它。
    fastplot::PlotWidget *plotWidget() const { return m_plot; }

Q_SIGNALS:
    // 视口尺寸变了：上层需要按新像素宽重新取 LOD。
    void viewportChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void pushToPlot();

    fastplot::PlotWidget *m_plot = nullptr;

    ng::core::DatasetId m_datasetId = ng::core::kInvalidDataset;
    QString m_table;
    QString m_column;
    QString m_title;

    QVector<ng::core::LodTriple> m_lod; // <= 像素宽度 个三元组
    double m_t0 = 0.0;
    double m_t1 = 0.0;
};

} // namespace ng::gui
