#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

// 一块画布（一页里的一个 plot 格）。
// M0 用 QPainter 自绘：只做坐标轴 + 曲线 + 十字光标 + 滚轮缩 Y / 拖动平移 Y，
// JKQTPlotter 接入后这层退化为数据源适配（D3）。
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const { return m_title; }

signals:
    void activated(const QString &title); // 被点中 → 属性面板切换对象

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Curve
    {
        QString name;
        QColor color;
        QVector<double> v;
    };

    QRectF plotRect() const;
    void resetView();

    QString m_title;
    int m_seed = 0;
    QVector<Curve> m_curves;
    double m_t0 = 0.0;
    double m_t1 = 30.0;
    double m_yMin = -1.6;
    double m_yMax = 1.6;
    bool m_dragging = false;
    bool m_hover = false;
    QPointF m_lastPos;
    QPointF m_cursor;
};
