#pragma once

#include "Dataset.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QVector>
#include <QWidget>
#include <QEvent>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QContextMenuEvent;

namespace nextpilot::plotscope::fastplot
{
class Axes;
class Zoomer;
class Panner;
class Picker;
}

// 中央绘图画布：把 ng 的 LOD 三元组交给 fastplot 绘制。
//
// 分工（这是 P2 的解耦点）：
//   - 抽稀在 Rust 内核（Dataset::lodMinMax），C++ 只得到 <= 像素宽 个三元组；
//   - 坐标变换、刻度、网格、包络绘制全在 fastplot 里，本类只做"取数 + 适配"。
//
// 内存铁律不变：C++ 侧每条曲线只持 <= 像素宽 个三元组，与数据总行数无关。
//
// 一个画布可叠加**多条曲线**（正常使用 plot 的核心）：每条曲线有独立的
// 数据源标识、颜色、名字与 LOD。追加曲线不覆盖既有曲线。曲线样式（颜色/
// 线宽/是否可见/挂左轴还是右轴）由上层按属性契约写入 PlotSeries.style。
//
// 它**不知道数据从哪来**，所以数据源变化由上层喂进来；
// 但它知道"自己多大"——视口尺寸变化会发 viewportChanged，
// 让上层按新的像素宽度重新取数（否则窄屏取的 LOD 会被拉伸成锯齿）。
namespace nextpilot::plotscope::gui
{

// 画布上的一条曲线：逻辑标识 + 有界 LOD + 样式。
struct PlotSeries
{
    nextpilot::plotscope::core::DatasetId datasetId = nextpilot::plotscope::core::kInvalidDataset;
    QString table;
    QString column;
    QString title;             // 完整标题（数据集 / table.column）
    QColor color;              // 曲线色（由上层按主题分配）
    QVector<nextpilot::plotscope::core::LodTriple> lod;
    double t0 = 0.0;
    double t1 = 0.0;
    bool visible = true;
    bool rightAxis = false;    // 挂右 y 轴
    qreal lineWidth = 1.4;

    // 曲线内变换（对齐需求：X 轴时间 offset，Y 轴算子/表达式/Lua）。
    // 执行位置：builtin/expression 走 Rust kernel，lua 走 C++ TransformEvaluator
    // （由 ChannelView/数据取数层在喂给画布前施加；这里只存配方与展示结果）。
    double xTimeOffset = 0.0;      // X 轴时间偏移（秒）
    QString yTransformId;          // Y 轴变换 descriptor id（TransformRegistry）
    QString yTransformParamsJson;  // 变换参数/表达式/Lua 源码（JSON 载荷）

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

class PlotCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit PlotCanvas(QWidget *parent = nullptr);
    ~PlotCanvas() override;

    // 追加/替换一条曲线的 LOD 数据（按 dataset+table+column 匹配，存在则更新）。
    // 返回曲线在序列中的索引。
    int setSeries(const PlotSeries &series);

    // 兼容入口：把 LOD 喂给首条曲线（旧调用方/测试用；无曲线时忽略）。
    void setLodData(const QVector<nextpilot::plotscope::core::LodTriple> &lod, double t0, double t1);

    // 从 Dataset 重新取数并刷新所有曲线（便捷入口）。
    void renderFrom(const nextpilot::plotscope::core::Dataset &dataset);

    // 从 Dataset 取 LOD 并追加（便捷入口，内部按画布像素宽取数）。
    // 保留旧签名以兼容：它等价于用单条序列替换全部。
    void setChannel(nextpilot::plotscope::core::DatasetId datasetId, const QString &table, const QString &column,
                    const QString &title);

    // 清空全部曲线。removeSeries 按 key 删除单条。
    void clear();
    bool removeSeries(const QString &key);

    // 把某条曲线挂到左轴或右轴（双 Y 轴的用户选择入口）。
    bool setSeriesAxis(const QString &key, bool rightAxis);

    // 曲线操作（plotting 全套操作意图的画布落点）：显隐 / 颜色 / 线宽 / 删除。
    bool setSeriesVisible(const QString &key, bool visible);
    bool setSeriesInk(const QString &key, const QColor &color);
    bool setSeriesWidth(const QString &key, double width);

    // 按 key 找曲线（属性面板/选中路由用）。
    const PlotSeries *seriesByKey(const QString &key) const;
    // 曲线内变换设置：X 时间 offset / Y 变换 descriptor id。
    bool setSeriesXOffset(const QString &key, double offsetSec);
    bool setSeriesYTransform(const QString &key, const QString &descriptorId,
                             const QString &paramsJson = QString());
    // 轴级变换（配方存画布视图状态；实际施加在曲线取数层）。
    void setAxisTransform(const QString &axis, const QString &descriptorId);
    QString axisTransform(const QString &axis) const;

    // 视图缩放/自适应：factor<1 放大、>1 缩小；reset 自适应全量。
    void zoomRange(double factor);
    void autoScale();

    // 图型：timeseries（折线）/ scatter（散点）等在画布内切换；
    // 返回 false 表示该 kind 不属于本画布（如仪表，需切到 InstrumentView）。
    bool setPlotKind(const QString &kind);
    QString plotKind() const { return m_plotKind; }

    // 曲线表（只读）。
    const QVector<PlotSeries> &series() const { return m_series; }
    int seriesCount() const { return m_series.size(); }
    const PlotSeries *seriesAt(int index) const;

    // 绘图区（不含坐标轴留白）的像素宽度。
    int plotPixelWidth() const;

    // 导出（fastplot 库能力的视图薄包装；写文件在画布侧，模块层不碰 QWidget）。
    bool exportPng(const QString &path);
    bool exportCsv(const QString &path);

    // 视图状态：只存视图（范围/轴/图例/背景），数据归宿主。
    QJsonObject saveViewState() const;
    bool restoreViewState(const QJsonObject &state);

    void setLegendVisible(bool visible);
    bool legendVisible() const;

    // 诊断/测试用。
    int lodPointCount() const; // 全部曲线的点数之和
    QString channelTitle() const { return m_title; }

    // 需要直接操作画布能力（缩放/导出等）时用；正常情况下上层不要碰它。
    nextpilot::plotscope::fastplot::Axes *plotWidget() const { return m_plot; }
    nextpilot::plotscope::fastplot::Zoomer *zoomer() const { return m_zoomer; }

    // 当前可见时间窗（秒）。
    bool visibleTimeRange(double *t0, double *t1) const;

    // 处理一路通道拖拽负载：解码合法则发 channelDropped，返回 true。
    bool dropChannel(const QMimeData *mime);

Q_SIGNALS:
    void viewportChanged();
    void rangeChanged(double t0, double t1);
    // 统一选中：右键菜单/交互选中了某条曲线或某个轴，交给上层转 AppContext selection。
    void seriesSelectionRequested(const QString &seriesKey);
    void axisSelectionRequested(const QString &axis); // "x" / "y-left" / "y-right"
    void canvasSelectionRequested();
    // 请求编辑曲线 Y 变换（上层打开变换编辑器后回写配方）。
    void seriesTransformRequested(const QString &seriesKey);
    // 有通道被拖到画布上（数据集树 → 画布）。画布只解码 MIME 发信号，
    // 取数由 ChannelView 订阅后走 plot.channel 意图——与双击同一路径。
    void channelDropped(nextpilot::plotscope::core::DatasetId datasetId, const QString &table,
                        const QString &column);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void pushToPlot();
    void applyPalette();
    // 为新曲线分配主题色（按当前已有曲线条数轮换）。
    QColor nextSeriesColor() const;

    nextpilot::plotscope::fastplot::Axes *m_plot = nullptr;
    nextpilot::plotscope::fastplot::Zoomer *m_zoomer = nullptr;
    nextpilot::plotscope::fastplot::Panner *m_panner = nullptr;
    nextpilot::plotscope::fastplot::Picker *m_picker = nullptr;

    QVector<PlotSeries> m_series; // 叠加的多条曲线
    QHash<QString, QString> m_axisTransforms; // axis -> transform descriptor id（视图配方）
    QString m_title;              // 画布标题（首条曲线标题，供状态显示）
    QString m_plotKind = QStringLiteral("timeseries"); // 当前图型（画布内可切的）
    double m_t0 = 0.0;
    double m_t1 = 0.0;
};

} // namespace nextpilot::plotscope::gui
