#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

struct PsChannelInfo;

// 一块画布（一页里的一个 plot 格）。
// 曲线数据来自内核（PsChannelInfo 指针，不拷贝）；视口抽稀调用 ps_lod_minmax，
// 只把 ≤px 个 (x,min,max) 三元组拿到 UI 侧画——对应 §11 铁律 3。
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const { return m_title; }

    // 用内核通道替换当前所有曲线；传空则只清空。
    void setChannels(const QVector<const PsChannelInfo *> &channels);
    void addChannel(const PsChannelInfo *channel);   // 字段树拖入时追加一条曲线
    void clearChannels();
    void removeChannel(int index);
    void toggleChannel(int index);
    bool hasData() const { return !m_series.isEmpty(); }
    bool kernelBacked() const { return m_kernelBacked; }  // 数据指针属于内核数据集
    void setTimeRange(double t0, double t1);             // 同页 X 轴联动时被动设置
    void requestAutoY();                                  // 按可见曲线量程重算 Y
    double t0() const { return m_t0; }
    double t1() const { return m_t1; }

signals:
    void activated(const QString &title);
    void timeRangeChanged(double t0, double t1);   // 用户缩放/拖动 X 后发出，供同页画布联动
    void addRequested();                            // 右键“添加画布”
    void removeRequested();                         // 右键“删除画布”

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Series
    {
        QString name;
        QString unit;
        QColor color;
        const double *t = nullptr; // 内核内存（或 demo 自有内存）
        const double *v = nullptr;
        quint32 n = 0;
        bool visible = true;
        QVector<double> tOwn;      // demo 数据所有权
        QVector<double> vOwn;

        const double *time() const { return t ? t : tOwn.constData(); }
        const double *value() const { return v ? v : vOwn.constData(); }
        quint32 count() const { return t ? n : static_cast<quint32>(qMin(tOwn.size(), vOwn.size())); }
    };

    QRectF plotRect() const;
    void resetView();
    void ensureTimeRange();
    void addDemoCurves();

    QString m_title;
    QVector<Series> m_series;
    double m_t0 = 0.0;
    double m_t1 = 30.0;
    double m_yMin = -1.6;
    double m_yMax = 1.6;
    bool m_autoY = true;
    bool m_showLegend = true;
    bool m_showGrid = true;
    bool m_kernelBacked = false;
    bool m_dragging = false;
    bool m_hover = false;
    QPointF m_lastPos;
    QPointF m_cursor;
    QVector<QRectF> m_legendRects;   // 图例命中区，随 paint 重算
    int m_legendHit = -1;
};
