#pragma once

#include <QColor>
#include <QFontMetrics>
#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QCursor>
#include <QPoint>
#include <QWidget>
#include "TimeBookmark.h"

class QToolButton;
class QKeyEvent;

struct PsChannelInfo;
struct PsDataset;

// 一块画布（一页里的一个 plot 格）。
// 曲线数据来自内核（PsChannelInfo 指针，不拷贝）；视口抽稀调用 ps_lod_minmax，
// 只把 ≤px 个 (x,min,max) 三元组拿到 UI 侧画——对应 §11 铁律 3。
class PlottingCanvas : public QWidget
{
    Q_OBJECT

public:
    enum PlotMode { TimeSeries, Xy, Scatter, BarChart, Histogram, Spectrum, Horizon, Compass, Gauge, Traj3D, GpsMap, Surface3D };
    explicit PlottingCanvas(QWidget *parent = nullptr);

    void setPlotMode(PlotMode mode, const QString &tag = QString());
    PlotMode plotMode() const { return m_mode; }
    QString plotModeTag() const { return m_modeTag; }

    void setTitle(const QString &title);
    QString title() const { return m_title; }
    void notifyActivated() { emit activated(m_title); }

    // 用内核通道替换当前所有曲线；传空则只清空。
    void setChannels(const QVector<const PsChannelInfo *> &channels);
    void addChannel(const PsChannelInfo *channel);   // 字段树拖入时追加一条曲线
    void addChannelEx(const PsChannelInfo *channel, int srcId, const QString &path,
                      const QString &alias, double offsetSec);  // 指定来源（多源对比）
    bool replaceChannel(const QString &oldName, const PsChannelInfo *channel); // 变换 Apply：替换并继承颜色
    QJsonObject seriesState(int index) const;             // 单条曲线属性快照
    bool patchSeries(int index, const QJsonObject &patch); // 改颜色/线宽/线型/轴/可见/时间偏移
    bool setSeriesChannel(int index, const PsChannelInfo *channel, int srcId,
                          const QString &srcPath, const QString &alias, double offsetSec);
    bool setSeriesSamples(int index, const QVector<double> &time, const QVector<double> &value,
                          const QString &label);          // 变换/脚本输出写回该曲线
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
    bool replaceGeneratedSeries(const QString &oldLabel, const QString &name,
                                const QVector<double> &time, const QVector<double> &value);
    void removeGeneratedSeries(const QString &name);
    void removeChannel(int index);
    void toggleChannel(int index);
    bool hasData() const { return !m_series.isEmpty(); }
    bool kernelBacked() const { return m_kernelBacked; }  // 数据指针属于内核数据集
    void setTimeRange(double t0, double t1);             // 同页 X 轴联动时被动设置
    void reloadDisplaySettings();                         // 设置页保存后刷新时间格式
    void requestAutoY();                                  // 按可见曲线量程重算 Y
    void setFollowLive(bool on, double windowSec = 30.0); // 实时滚动：窗口跟随最新样本
    void followLatest() { if (m_followLive) resetView(); }
    void setGridVisible(bool on);
    void setGridAxes(bool xGrid, bool yGrid, bool minorGrid);
    bool xGrid() const { return m_gridX; }
    bool yGrid() const { return m_gridY; }
    bool minorGrid() const { return m_minorGrid; }
    void setLegendPosition(int pos);
    int legendPosition() const { return m_legendPos; }
    void setAxisFontSize(int px);
    int axisFontSize() const { return m_axisFontSize; }
    void setAxisLineWidth(int px);
    int axisLineWidth() const { return m_axisLineWidth; }
    void setAxisColor(const QColor &c);
    QColor axisColor() const { return m_axisColor; }

private:
    QColor axisInk() const;
public:
    void setLegendVisible(bool on);
    void setBoxVisible(bool on);
    void setFlipY(bool on);
    bool flipY() const { return m_flipY; }
    void setAxisLabels(const QString &xLabel, const QString &yLabel);
    QString xLabel() const { return m_xLabel; }
    QString yLabel() const { return m_yLabel; }
    void setRubberZoomEnabled(bool on);
    // 鼠标工具：-1=无工具 0=平移(手) 1=放大(镜) 2=缩小(镜)。按钮可再次点击退出
    enum Tool { NoTool = -1, PanTool = 0, ZoomInTool = 1, ZoomOutTool = 2 };
    void setTool(int tool);
    int tool() const { return m_tool; }
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
    QRectF plotArea() const { return plotRect(); }
    double yMin() const { return m_yMin; }
    double yMax() const { return m_yMax; }
    bool measurementEnabled() const { return m_abEnabled; }
    double measurementA() const { return m_abA; }
    double measurementB() const { return m_abB; }
    double t1() const { return m_t1; }
    void setPlayhead(double t);
    void setMaximizedState(bool on);                // 主窗切换后同步按钮图标/提示
    void clearPlayhead();
    void setTimeMarkers(const QVector<ps::TimeBookmark> &markers);
    void setLinkedCursor(double t, bool visible);       // 同页其他画布联动的时间光标
    bool linkedCursorVisible() const { return m_linkedCursorVisible; }
    double linkedCursorTime() const { return m_linkedCursorTime; }
    void toggleReadoutPin();                             // 固定/解除当前读数
    bool readoutPinned() const { return m_pinnedReadout; }
    double pinnedReadoutTime() const { return m_pinnedTime; }

signals:
    void activated(const QString &title);
    void seriesSelected(int index);
    void timeRangeChanged(double t0, double t1);   // 用户缩放/拖动 X 后发出，供同页画布联动
    void addRequested();                            // 右键“添加画布”
    void addChannelRequested();                     // 右键“添加曲线…”：由主窗选通道
    void removeRequested();                         // 右键“删除画布”
    void splitRequested(int orientation);           // 0=左右拆分 1=上下拆分
    void maximizeRequested();                       // 最大化 / 还原本画布
    void markerRequested(double t);                 // 右键：在指针处添加时间书签
    void markerRemoveRequested(const QString &id);   // 右键：删除书签
    void markerMoveRequested(const QString &id, double to); // 拖动书签后提交
    void markerRenameRequested(const QString &id, const QString &label); // 右键：重命名书签
    void hoverTimeChanged(double t, bool visible);       // 时间轴悬停，同页画布联动

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
        const PsChannelInfo *info = nullptr; // 实时通道：point_count 会增长
        const PsDataset *dataset = nullptr;   // typed 表所属数据集；旧通道为空走旧 LOD
        QString table;             // typed 表名
        QString column;            // typed 列名
        quint64 generation = 0;    // 数据源重建代数
        const double *t = nullptr; // 内核内存（或 demo 自有内存）
        const double *v = nullptr;
        quint32 n = 0;
        bool visible = true;
        int width = 1;
        int axis = 0;              // 0=左轴，1=右轴
        int lineStyle = 0;         // 0=实线，1=虚线，2=点线，3=点划线
        int marker = 0;            // 0=无，1=圆，2=方，3=菱形，4=三角，5=十字，6=叉
        int markerSize = 5;

        QVector<double> tOwn;      // demo 数据所有权
        QVector<double> vOwn;

        bool typed() const { return dataset && !table.isEmpty() && !column.isEmpty(); }
        double timeAt(quint32 i) const;
        double valueAt(quint32 i) const;
        const double *time() const { return t ? t : tOwn.constData(); }
        const double *value() const { return v ? v : vOwn.constData(); }
        quint32 count() const
        {
            if (typed())
                return n;
            return t ? n : static_cast<quint32>(qMin(tOwn.size(), vOwn.size()));
        }
    };

    enum AxisZone { NoAxis = 0, XAxis, YAxisLeft, YAxisRight };
    enum RightHit { RightBlank = 0, RightLegend, RightSeries, RightMarker, RightAxis, RightAb };
    QRectF plotRect() const;
    AxisZone axisZoneAt(const QPointF &pos) const;
    double timeAtX(double x) const;
    int seriesAt(const QPointF &pos) const;
    int markerAt(const QPointF &pos) const;
    void jumpToBookmark(int index);
    void selectBookmark(int index);
    void clearBookmarkSelection();
    void createMeasurementAt(double t);
    void clearMeasurement();
    double snapTimeToSample(double t, bool force = false) const;
    void resetXAxis();
    void resetYAxis();
    void applyBoxZoom(const QRectF &r, AxisZone zone, const QPointF &start, const QPointF &end);
    void showLegendMenu(int index, const QPoint &globalPos);
    void showSeriesMenu(int index, const QPoint &globalPos);
    void showCanvasMenu(double t, int markerIndex, AxisZone zone, const QPoint &globalPos);
    void showAxisMenu(AxisZone zone, const QPoint &globalPos);
    uint32_t kernelLod(const Series &s, double t0, double t1, uint32_t px,
                       double *out, uint32_t cap) const;
    double sampleAt(const Series &s, double displayT, bool *ok) const;          // 右键命中哪条曲线
    QString formatTime(double t) const;
    void resetView();
    void ensureTimeRange();
    bool hasRightAxis() const;
    int seriesAxisCount() const;
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
    void paintGpsMap(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void paintSurface3D(QPainter &p, const QRectF &plot, const QFontMetrics &fm);
    void buildHoverBar();
    void positionHoverBar();
    void reorderByCursor(const QPoint &globalPos);
    double lastValue(const QString &namePart, bool *ok) const;

    QString m_title;
    QString m_timeFormat = QStringLiteral("relative");
    QVector<Series> m_series;
    QVector<ps::TimeBookmark> m_timeMarkers;
    QVector<QPair<QString, QRectF>> m_markerRects;
    int m_selectedSeries = -1;
    double m_t0 = 0.0;
    double m_t1 = 30.0;
    double m_yMin = -1.6;
    double m_yMax = 1.6;
    double m_y2Min = -1.6;
    double m_y2Max = 1.6;
    bool m_autoY = true;
    bool m_showLegend = true;
    bool m_showGrid = true;
    bool m_gridX = true;
    bool m_gridY = true;
    bool m_minorGrid = false;
    int m_legendPos = 0;         // 0=左上，1=右上，2=左下，3=右下
    int m_axisFontSize = 11;
    int m_axisLineWidth = 1;
    QColor m_axisColor;          // 无效时跟随主题
    bool m_showBox = true;
    bool m_flipY = false;
    QString m_xLabel;
    QString m_yLabel;
    bool m_rubberZoom = false;
    int m_tool = NoTool;
    bool m_followLive = false;
    double m_liveWindow = 30.0;
    bool m_rubbering = false;
    bool m_abEnabled = false;      // A/B 游标测量：true=线可见且可编辑
    bool m_abHasA = false;
    bool m_abHasB = false;
    int m_abDrag = 0;              // 1=A 2=B，拖动游标
    double m_abA = 0.0;
    double m_abB = 0.0;
    bool m_hasPlayhead = false;
    double m_playhead = 0.0;
    QCursor toolCursor() const;
    QPointF m_rubberStart;
    QRectF m_rubberRect;
    AxisZone m_dragAxis = NoAxis;
    bool m_kernelBacked = false;
    PlotMode m_mode = TimeSeries;
    QString m_modeTag;
    bool m_dragging = false;
    bool m_spacePan = false;
    bool m_rightPressed = false;
    bool m_rightBoxing = false;
    bool m_rightCanBox = false;
    QPointF m_rightStart;
    QRectF m_rightRect;
    RightHit m_rightHit = RightBlank;
    int m_rightHitIndex = -1;
    int m_rightMarkerIndex = -1;
    AxisZone m_rightAxis = NoAxis;
    int m_bookmarkDragIndex = -1;
    QString m_bookmarkDragId;
    bool m_bookmarkDragActive = false;
    bool m_hasSelectedBookmark = false;
    QString m_selectedBookmarkId;
    QPointF m_bookmarkDragStart;
    bool m_hover = false;
    bool m_pinnedReadout = false;
    QPointF m_pinnedCursor;
    double m_pinnedTime = 0.0;
    bool m_linkedCursorVisible = false;
    double m_linkedCursorTime = 0.0;
    QWidget *m_hoverBar = nullptr;
    QToolButton *m_dragBtn = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QToolButton *m_abBtn = nullptr;
    bool m_maximized = false;
    bool m_reordering = false;
    bool m_titleDrag = false;
    QPointF m_titleDragStart;
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
