#pragma once

#include <QColor>
#include <QFontMetrics>
#include <QHash>
#include <QJsonObject>
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
    enum PlotMode { TimeSeries, Xy, Scatter, BarChart, Histogram, Spectrum, Horizon, Compass, Gauge, Traj3D };
    explicit CanvasWidget(QWidget *parent = nullptr);

    void setPlotMode(PlotMode mode, const QString &tag = QString());
    PlotMode plotMode() const { return m_mode; }
    QString plotModeTag() const { return m_modeTag; }

    void setTitle(const QString &title);
    QString title() const { return m_title; }

    // 用内核通道替换当前所有曲线；传空则只清空。
    void setChannels(const QVector<const PsChannelInfo *> &channels);
    void addChannel(const PsChannelInfo *channel);   // 字段树拖入时追加一条曲线
    void addChannelEx(const PsChannelInfo *channel, int srcId, const QString &path,
                      const QString &alias, double offsetSec);  // 指定来源（多源对比）
    bool replaceChannel(const QString &oldName, const PsChannelInfo *channel); // 变换 Apply：替换并继承颜色
    void removeChannel(const QString &name);
    void setInputSource(int id, const QString &path, const QString &alias = QString(),
                        double offsetSec = 0.0); // 当前拖拽来源
    void setSourceOffset(const QString &path, double offsetSec, bool refit = false);
    void removeChannelsFromSource(const QString &path);
    void renameSource(const QString &path, const QString &alias);
    void clearChannels();
    // 脚本/派生输出：时间与值由 UI 侧持有，不指向内核内存
    struct SampleSeries { QString name; QVector<double> time; QVector<double> value; };
    QVector<SampleSeries> sampleSeries() const;
    void addGeneratedSeries(const QString &name, const QVector<double> &time,
                            const QVector<double> &value);
    void removeChannel(int index);
    void toggleChannel(int index);
    bool hasData() const { return !m_series.isEmpty(); }
    bool kernelBacked() const { return m_kernelBacked; }  // 数据指针属于内核数据集
    void setTimeRange(double t0, double t1);             // 同页 X 轴联动时被动设置
    void requestAutoY();                                  // 按可见曲线量程重算 Y
    void setGridVisible(bool on);
    void setLegendVisible(bool on);
    void setBoxVisible(bool on);
    void setRubberZoomEnabled(bool on);
    void exportPng();                                   // 当前画布渲染结果另存
    void exportCsv();                                   // 当前可见时间窗内的长表 CSV
    void zoomBy(double factor);                          // 1.25 放大，0.8 缩小
    bool gridVisible() const { return m_showGrid; }
    bool legendVisible() const { return m_showLegend; }
    bool boxVisible() const { return m_showBox; }
    bool rubberZoomEnabled() const { return m_rubberZoom; }
    QJsonObject saveState() const;                        // 布局/曲线快照
    void restoreState(const QJsonObject &o, const QHash<QString, const PsChannelInfo *> &pool);
    double t0() const { return m_t0; }
    double t1() const { return m_t1; }

signals:
    void activated(const QString &title);
    void timeRangeChanged(double t0, double t1);   // 用户缩放/拖动 X 后发出，供同页画布联动
    void addRequested();                            // 右键“添加画布”
    void addChannelRequested();                     // 右键“添加曲线…”：由主窗选通道
    void removeRequested();                         // 右键“删除画布”
    void splitRequested(int orientation);           // 0=左右拆分 1=上下拆分

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
        QString name;              // 原始通道名（查数据用）
        QString label;             // 图例显示名：源别名 · 通道名
        QString unit;
        int srcId = -1;            // 运行期数据源 id
        QString srcPath;           // 布局持久化用：源文件路径
        QColor color;
        double offsetSec = 0.0;    // 显示时间 = 原始时间 - offset
        const double *t = nullptr; // 内核内存（或 demo 自有内存）
        const double *v = nullptr;
        quint32 n = 0;
        bool visible = true;
        int width = 1;
        QVector<double> tOwn;      // demo 数据所有权
        QVector<double> vOwn;

        const double *time() const { return t ? t : tOwn.constData(); }
        const double *value() const { return v ? v : vOwn.constData(); }
        quint32 count() const { return t ? n : static_cast<quint32>(qMin(tOwn.size(), vOwn.size())); }
    };

    QRectF plotRect() const;
    int seriesAt(const QPointF &pos) const;
    double sampleAt(const Series &s, double displayT, bool *ok) const;          // 右键命中哪条曲线
    void resetView();
    void ensureTimeRange();
    void addDemoCurves();
    void paintSpecial(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintXy(QPainter &p, const QRectF &plot, const QFontMetrics &fm, bool scatter);
    void paintBar(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintHistogram(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintSpectrum(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintHorizon(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintCompass(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintGauge(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintTraj3D(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    double lastValue(const QString &namePart, bool *ok) const;

    QString m_title;
    QVector<Series> m_series;
    double m_t0 = 0.0;
    double m_t1 = 30.0;
    double m_yMin = -1.6;
    double m_yMax = 1.6;
    bool m_autoY = true;
    bool m_showLegend = true;
    bool m_showGrid = true;
    bool m_showBox = true;
    bool m_rubberZoom = false;
    bool m_rubbering = false;
    bool m_abEnabled = false;      // A/B 游标测量
    bool m_abHasA = false;
    bool m_abHasB = false;
    double m_abA = 0.0;
    double m_abB = 0.0;
    QPointF m_rubberStart;
    QRectF m_rubberRect;
    bool m_kernelBacked = false;
    PlotMode m_mode = TimeSeries;
    QString m_modeTag;
    bool m_dragging = false;
    bool m_hover = false;
    QPointF m_lastPos;
    QPointF m_cursor;
    QVector<QRectF> m_legendRects;   // 图例命中区，随 paint 重算
    QVector<QPolygonF> m_hitPaths;   // 最近一帧的曲线折线，用于右键命中
    QVector<int> m_hitOwner;         // m_hitPaths 对应的 series 下标
    int m_legendHit = -1;
    int m_inputSrcId = -1;
    QString m_inputSrcPath;
    QString m_inputSrcAlias;
    double m_inputOffset = 0.0;
};
