#pragma once

#include "Dataset.h"
#include "axes/PlotSeries.h"
#include "axes/CursorReadout.h"
#include "axes/SeriesStore.h"
#include "axes/ViewState.h"
#include "fastplot/Axes.h"

#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QWidget>
#include <QEvent>

#include <functional>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QContextMenuEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

namespace nextpilot::plotscope::fastplot
{
class Axes;
class Zoomer;
class Panner;
class Picker;
}

// 中央绘图画布：把 LOD 三元组交给 fastplot 绘制。
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

class Interaction;

class AxesView : public QWidget
{
    Q_OBJECT
public:
    explicit AxesView(QWidget *parent = nullptr);
    ~AxesView() override;

    // 追加/替换一条曲线的 LOD 数据（按 dataset+table+column 匹配，存在则更新）。
    // 返回曲线在序列中的索引。
    int setSeries(const PlotSeries &series);

    // 兼容入口：把 LOD 喂给首条曲线（旧调用方/测试用；无曲线时忽略）。
    void setLodData(const QVector<nextpilot::plotscope::dataset::LodTriple> &lod, double t0, double t1);

    // 从 Dataset 重新取数并刷新所有曲线（便捷入口）。
    void renderFrom(const nextpilot::plotscope::dataset::Dataset &dataset);

    // 从 Dataset 取 LOD 并追加（便捷入口，内部按画布像素宽取数）。
    // 保留旧签名以兼容：它等价于用单条序列替换全部。
    void setChannel(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table, const QString &column,
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
    // 源级时间 offset provider（第二梯队 #204 多源时间对齐）：
    // 画布把 datasetId 反查到源，取源 offset 叠加到曲线 x 上。
    // 由上层注入（PlotWorkspace 用 AppContext），画布不依赖 AppContext。
    void setSourceOffsetProvider(std::function<double(nextpilot::plotscope::dataset::DatasetId)> provider);
    // 源 offset 变化后重推曲线（LOD 不变，只重算 x 映射）。
    void refreshSourceOffsets();
    // 时间轴刻度三态："relative"（默认，相对秒）/
    // "hms"（H:MM:SS，从 0 秒起算）/ "utc"（无 UTC 基准，暂与 hms 同形并注明）。
    // 只改刻度文本，不动刻度位置与取数；每次 renderAll 前按 settings 重设。
    void setTimeFormatMode(const QString &mode);
    QString timeFormatMode() const { return m_viewState->timeFormat(); }
    void applyTimeFormat();
    void applyTimeFormatToAxes();
    // 测试用：按当前时间格式把秒值翻成 X 轴刻度文本（无 formatter 时返回空串）。
    QString tickLabelForTest(double seconds) const;
    // 图例交互（第二梯队 #205，。
    bool toggleSeriesVisible(const QString &key); // 按 seriesKey 切显隐（同步 PlotSeries.visible）
    void setLegendCollapsed(bool collapsed) { if (m_plot) m_plot->setLegendCollapsed(collapsed); }
    bool legendCollapsed() const { return m_plot ? m_plot->legendCollapsed() : false; }
    void zoomLegendFont(int steps) { if (m_plot) m_plot->zoomLegendFont(steps); }
    double legendFontScale() const { return m_plot ? m_plot->legendFontScale() : 1.0; }
    // 轴级变换（配方存画布视图状态；实际施加在曲线取数层）。
    void setAxisTransform(const QString &axis, const QString &descriptorId);
    QString axisTransform(const QString &axis) const;

    // 视图缩放/自适应：factor<1 放大、>1 缩小；reset 自适应全量。
    void zoomRange(double factor);
    void autoScale();

    // 缩放/平移撤销：Ctrl+Z 回退上一次视图范围。
    bool canUndoView() const;
    void undoView();
    int viewHistorySize() const; // 诊断/测试用

    // 播放头竖线：回放时由播放引擎统一驱动所有画布，active=false 隐藏。
    void setPlayhead(double x, bool active);
    bool playheadActive() const;

    // ---- 游标读数 / A-B / 书签 交互（对齐老 PlottingCanvas 打磨体验）----
    // 取值从本画布曲线的有界窗口做最近样本查找（不插值），交 fastplot 画读数框。
    void setCursorTime(double t, bool pinned);      // 悬停/固定游标；pinned=false 时 t 为最近样本吸附后的时间
    void clearCursor();
    // 联动：静默同步同页其它画布的时间竖线（不复制读数、不回广播），避免信号回环。
    void syncLinkedTime(double t, bool visible);
    bool cursorPinned() const { return m_readout->pinned(); }
    double cursorTime() const { return m_readout->cursorTime(); }
    bool cursorActiveForTest() const { return m_readout->active(); }

    // 图例交互测试辅助：直接暴露 fastplot Axes 与 series 快照。
    nextpilot::plotscope::fastplot::Axes *plotForTest() const { return m_plot; }
    const PlotSeries &seriesAtForTest(int index) const { return m_seriesStore->all().at(index); }
    void refreshSeriesForTest() { pushToPlot(); }

    void toggleAB();                                // Ctrl+左键 / view action：以当前游标为中心生成一组 A/B
    void clearAB();
    bool abActive() const { return m_readout->abEnabled(); }

    struct Bookmark
    {
        QString id;
        double t = 0.0;
        QString label;
    };
    void addBookmark(double t, const QString &label = QString());
    void removeBookmark(const QString &id);
    void renameBookmark(const QString &id, const QString &label);
    const QVector<Bookmark> &bookmarks() const;

    // Marker 规则产出：把 PlotMarker JSON 数组交给 fastplot 绘制（竖线/区间带/值带/label）。
    void setMarkers(const QJsonArray &markers);
    int markerCount() const;

    // 状态视图：rows JSON 数组
    // [{channel, segments:[{tStart,tEnd,label}]}]，画到画布顶部色带区。
    // append=true 时按 channel 名合并（同名单替换、新名追加），多行叠加。
    bool setStateRows(const QJsonArray &rows, bool append = false);
    int stateRowCount() const;

    // hit-test：给像素位置，返回命中的读数对象（供右键菜单/拖动仲裁）。
    enum class Hit { None, CursorLine, AB_A, AB_B, Bookmark };
    struct HitResult
    {
        Hit kind = Hit::None;
        QString bookmarkId;
    };
    HitResult hitTest(const QPointF &pixel) const;
    // 把像素 x 换算为数据时间（交互部件据此生成 A/B、定位书签）。
    double timeAtPixel(double px) const;

    // 图型：timeseries（折线）/ scatter（散点）等在画布内切换；
    // 返回 false 表示该 kind 不属于本画布（如仪表，需切到 InstrumentView）。
    bool setPlotKind(const QString &kind);
    QString plotKind() const { return m_viewState->plotKind(); }

    // 曲线表（只读）。
    const QVector<PlotSeries> &series() const { return m_seriesStore->all(); }
    int seriesCount() const { return m_seriesStore->count(); }
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
    // 图例四角：0=右上 1=左上 2=左下 3=右下（Legend::Corner 序）；越界忽略。
    void setLegendCorner(int corner);
    int legendCorner() const;
    // 参考线（第3仗 3c / F-2）：存相对时间轴值；读数框显示相对 Δ；随视图状态持久化。
    void setReferenceTime(double tSec);
    void clearReference();
    bool hasReference() const;
    // 读数框纵向锚点（第3仗 3c / E-4）：游标像素 Y，框限速滑动跟随；<0 回顶部锚位。
    void setCursorPixelY(double pixelY);

    // "删除此画布"是否可点：页内只剩一格时删无可删，由 PlotWorkspace 重排后同步。
    void setCanvasRemovable(bool on) { m_canvasRemovable = on; }
    bool canvasRemovable() const { return m_canvasRemovable; }

    // 诊断/测试用。
    int lodPointCount() const; // 全部曲线的点数之和
    QString channelTitle() const { return m_title; }

    // 画布标题（axes 标题栏）。双击标题栏改名走这里：override 非空时优先于
    // "首条曲线标题"，空串=恢复自动标题。与 PJ4 的标题双击改名同一语义。
    void setCanvasTitle(const QString &title);
    QString canvasTitle() const { return m_viewState->titleOverride().isEmpty() ? m_title : m_viewState->titleOverride(); }
    bool hasTitleOverride() const { return !m_viewState->titleOverride().isEmpty(); }

    // 需要直接操作画布能力（缩放/导出等）时用；正常情况下上层不要碰它。
    nextpilot::plotscope::fastplot::Axes *plotWidget() const { return m_plot; }
    nextpilot::plotscope::fastplot::Zoomer *zoomer() const { return m_zoomer; }
    nextpilot::plotscope::fastplot::Panner *panner() const { return m_panner; }

    // 当前可见时间窗（秒）。
    bool visibleTimeRange(double *t0, double *t1) const;

    // 处理一路通道拖拽负载：解码合法则发 channelDropped，返回 true。
    // 若负载是"画布间曲线移动"（series_mime），发 seriesTransferRequested。
    bool dropChannel(const QMimeData *mime, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    // 画布身份：跨画布移动曲线时用来判断"拖回自己"和找回源画布。
    // id 由 PlotWorkspace 分配（全进程单调递增），不参与持久化。
    void setCanvasId(int id) { m_canvasId = id; }
    int canvasId() const { return m_canvasId; }

Q_SIGNALS:
    void viewportChanged();
    void rangeChanged(double t0, double t1);
    // 统一选中：右键菜单/交互选中了某条曲线或某个轴，交给上层转 AppContext selection。
    void seriesSelectionRequested(const QString &seriesKey);
    void axisSelectionRequested(const QString &axis); // "x" / "y-left" / "y-right"
    void canvasSelectionRequested();
    // 请求编辑曲线 Y 变换（上层打开变换编辑器后回写配方）。
    void seriesTransformRequested(const QString &seriesKey);
    // 画布级命令（拆分 / 删除本画布）。画布自己不知道自己在页里的索引，
    // 交给 PlotWorkspace 按指针反查后再 setActive + split/removeAt——
    // 这样右键"删除此画布"删的是你点的那个，而不是"碰巧激活"的另一个。
    void canvasActionRequested(const QString &op);
    // 有通道被拖到画布上（数据集树 → 画布）。画布只解码 MIME 发信号，
    // 取数由 ChannelView 订阅后走 plot.channel 意图——与双击同一路径。
    void channelDropped(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table,
                        const QString &column);
    // Ctrl+拖通道 = XY 建图手势（第3仗 3c / F-1）：本画布首曲线作 X，落下的通道作 Y，
    // 工作区建独立 XY 画布（对齐 PJ4 createCurveXYInteractive 的"拖两通道"体验）。
    void xyDropRequested(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table,
                         const QString &column);
    // 曲线被拖到别的画布：由工作区执行"目标加一条、源删一条"（移动不是复制）。
    void seriesTransferRequested(int srcCanvasId, const QString &key, int datasetId,
                                 const QString &table, const QString &column,
                                 const QString &title);
    // 联动：本画布时间变化通知同页其它画布（source 用于避免回环，只同步竖线不复制读数）。
    void linkedTimeChanged(double t, bool visible, AxesView *source);
    // 书签集合变化（上层存进布局）。
    void bookmarksChanged();
    // 图例点击切换曲线显隐：上层可同步图例面板/属性面板状态。
    void seriesVisibilityChanged(const QString &seriesKey, bool visible);
    // 曲线增删/样式/挂轴变更（SeriesStore::changed 转发）：全局 undo 动作流入口之一。
    void seriesMutated();
    // 鼠标在绘图区内移动（fastplot Picker 每帧回调）：上层据此浮出该格的悬浮工具栏。
    // 用信号而不是事件过滤器——鼠标事件被 Axes/Picker 吃掉，父控件收不到 MouseMove。
    void pointerHovered();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    friend class Interaction; // 交互部件要读写画布内部状态（PJ4 DockWidget 同做法）

    void pushToPlot();
    void applyPalette();
    // 为新曲线分配主题色（按当前已有曲线条数轮换）。
    QColor nextSeriesColor() const;
    // 读数：从曲线有界窗口取 t 时刻最近样本，重写叠加层。
    void refreshReadout();
    // 源级时间 offset：datasetId 经 provider 反查（无 provider 返回 0）。
    double sourceOffsetFor(const PlotSeries &s) const;
    // 交互状态机入口。
    // 视图范围历史：记录每次缩放/平移"之前"的范围，供撤销恢复。
    void pushViewHistory();

    nextpilot::plotscope::fastplot::Axes *m_plot = nullptr;
    nextpilot::plotscope::fastplot::Zoomer *m_zoomer = nullptr;
    nextpilot::plotscope::fastplot::Panner *m_panner = nullptr;
    nextpilot::plotscope::fastplot::Picker *m_picker = nullptr;
    // 源级时间 offset provider：datasetId -> 源 offset（秒）。空 = 无源 offset。
    std::function<double(nextpilot::plotscope::dataset::DatasetId)> m_sourceOffsetProvider;

    SeriesStore *m_seriesStore = nullptr; // 曲线集合（数据与样式）
    ViewState *m_viewState = nullptr;     // 视图状态（范围历史/图型/刻度/标题/轴配方）
    CursorReadout *m_readout = nullptr;   // 读数（游标/A-B/书签标记的渲染）
    Interaction *m_interaction = nullptr; // 交互（鼠标状态机/键盘/右键菜单/书签）
    // 游标读数/A-B/书签交互状态（对齐老 PlottingCanvas）。
    bool m_canvasRemovable = true;   // 见 setCanvasRemovable()
    QVector<fastplot::Axes::StateRowMark> m_stateRows; // 状态视图行（append 合并语义）
    int m_canvasId = -1;
    QString m_title;              // 画布标题（首条曲线标题，供状态显示）
    double m_t0 = 0.0;
    double m_t1 = 0.0;

    // 恢复链：对标 PJ4 applySavedViewportOrZoom 在 bind/drain 后重放视口。
    // m_restoreInProgress 覆盖 restoreViewState 全程（其内部的 setAxis/配方恢复
    // 会多次触发 pushToPlot），期间一律不用数据全范围覆盖视口；
    // m_preserveRangeOnNextPush 在恢复末尾置位，保住"恢复后紧随的一次绑定重建"。
    bool m_restoreInProgress = false;
    bool m_preserveRangeOnNextPush = false;
    // 显隐单一出口回声守卫：setSeriesVisible → setLegendItemVisible 会发回声信号，
    // 回声重入本出口时被吞，避免重复同步/重复广播（第3仗 3a）。
    bool m_syncingVisibility = false;
};

} // namespace nextpilot::plotscope::gui
