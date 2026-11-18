#pragma once

#include "Dataset.h"

#include <QColor>
#include <QVector>
#include <QWidget>

// 中央绘图画布：QPainter 直接绘制 Rust LOD 给出的像素级三元组。
//
// 内存铁律（与整个架构一致）：
//   画布只持有 <= 像素宽度 个 (x,min,max) 三元组，与数据总行数无关。
//   百万行数据的绘制成本 = O(像素宽)，不是 O(行数)。
//   抽稀（min/max 分桶）在 Rust kernel 内完成，C++ 只画最终像素。
//
// 职责单一：只负责"把一条曲线的 LOD 三元组画出来"，不加载数据、
// 不认识格式、不做交互逻辑（交互由上层转发意图）。
namespace ng::gui
{

class PlotCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit PlotCanvas(QWidget *parent = nullptr);

    // 设置要绘制的数据集与通道（只存标识，不取数）。
    void setChannel(ng::core::DatasetId datasetId, const QString &table, const QString &column,
                    const QString &title);

    // 直接喂入 LOD 三元组（来自 Dataset::lodMinMax）。
    void setLodData(const QVector<ng::core::LodTriple> &lod, double t0, double t1);

    // 从 Dataset 取 LOD 并绘制（便捷入口，内部按画布像素宽取数）。
    void renderFrom(const ng::core::Dataset &dataset);

    // 清空。
    void clear();

    // 诊断/测试用。
    int lodPointCount() const { return m_lod.size(); }
    QString channelTitle() const { return m_title; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ng::core::DatasetId m_datasetId = ng::core::kInvalidDataset;
    QString m_table;
    QString m_column;
    QString m_title;

    QVector<ng::core::LodTriple> m_lod; // <= 像素宽度 个三元组
    double m_t0 = 0.0;
    double m_t1 = 0.0;
};

} // namespace ng::gui
