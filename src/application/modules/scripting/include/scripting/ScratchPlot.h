#pragma once

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

namespace ps::scripting
{

// scratch 临时图：只画一条曲线，不碰通道模型与画布状态。
// 频率域结果用 xKind="Hz"，刻度按频率格式化。
class ScratchPlot final : public QWidget
{
    Q_OBJECT

public:
    explicit ScratchPlot(QWidget *parent = nullptr);

    void setSeries(const QVector<QPointF> &points, const QString &title, const QString &xUnit);
    void clearSeries();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPointF> m_points;
    QString m_title;
    QString m_xUnit;
};

} // namespace ps::scripting
