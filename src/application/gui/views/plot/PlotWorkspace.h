#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <QVector>

class QScrollArea;
class QLabel;
class QStackedWidget;
class QTimer;
class QEvent;

namespace nextpilot::plotscope::plot
{
struct PlotDocumentSpec;
}
namespace nextpilot::plotscope::dataset
{
class SourceHub;
}

namespace nextpilot::plotscope::gui
{

class AxesToolbar;
class ChannelView;
class PlotLayout;
class TabbedFigure;
class DockedAxes;
class InstrumentView;
class AxesView;
class PlaybackController;

// 画布工作区：中央区域的组合容器。
//
// 层次：Tab（绘图页）→ 页内 QSplitter 树（m×n 网格 / 手动拆分）→ Cell（一块画布）
//
// 为什么要这一层（而不是让外壳直接摆一个 AxesView）：
//   - figure.new / 拆分 / 网格排布是"每页 N 个画布"，一个画布承载不了；
//   - 页签是**工作视图**的单位：同一份数据可以按不同页看不同角度（总览/局部/仪表），
//     切页不重新载入数据，也不复制数据；
//   - view action（plot.*）若由每个画布各自注册，后建的会劫持先建的（QHash 覆盖）——
//     必须由**唯一仲裁者**集中注册一次，路由到当前活动 cell；
//   - 活动通道（数据集树双击/拖拽）只追加到活动 cell，符合"当前画布"语义。
//
// 分工：本类只做布局与路由（QWidget 组合），取数仍走 ChannelView→意图层，
// 绘制仍是 AxesView→fastplot。多画布不复制数据、不新增数据概念。
class PlotWorkspace : public QWidget
{
    Q_OBJECT
public:
    PlotWorkspace(context::AppContext &appCtx, intent::IIntentExecutor &executor,
                    QWidget *parent = nullptr);
    ~PlotWorkspace() override;

    // 当前页内的画布格数（老 cellCount 的语义收窄到"当前页"）。
    int cellCount() const;
    int pageCount() const;
    int activeIndex() const;
    // 当前活动画布（无画布时返回 nullptr）。仅供视图内部路由，不交给外壳。
    AxesView *activeCanvas() const;
    ChannelView *activeChannelView() const;

    // 程序化浮出活动格的悬浮工具栏（命令面板/截图/自动化验收用）。
    // autoHide=false 时不做自动收起，留给人看或截图。
    void flashToolbar(bool autoHide = true);

    // 当前页网格规格（行×列），截图/状态显示用。
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    // 设为活动 cell（点击/程序指定）。view action 处理器也用；测试需显式定位画布。
    void setActive(int index);

    // ---- 文档作用域双向通道（R3）----
    // 导出：遍历全部页的格，按视图读 viewType/channels/style/state，数据集按 origin 引用。
    // 恢复：按 spec 建页建格（v1 按 rows×cols 等分），逐格回绑 origin 对应的数据集。
    // 两条通道只覆盖"页/格/视图/曲线"，ADS 浮动与合并仍归 dock 布局持久化。
    plot::PlotDocumentSpec toDocumentSpec() const;
    bool applyDocumentSpec(const plot::PlotDocumentSpec &spec,
                           const dataset::SourceHub &sources);

    // ---- 晚到绑定（pending binder，对标 PJ4 PendingDisplayBinder）----
    //
    // 场景：布局先到、数据后到——layout 加载时 origin 对应的源还没载入
    // （换数据文件 / live 流未连）。applyDocumentSpec 此时把画布留空态并把
    // 该格的绑定意图挂进 pending 队列；源一登记（sourceAdded/datasetAdded
    // 信号）就按 origin 自动接回曲线、回放存盘视口。
    //
    // demand reference：每个 pending 条目在 SourceHub 上按 origin 计一个
    // 需求引用（见 SourceHub::demandOrigin）——源被移除后需求仍在，
    // 同 origin 的源再次出现即自动接回。
    int pendingBindCount() const;
    // 挂起中等待的 origin（诊断/preflight 用）。
    QStringList pendingBindOrigins() const;

    // ---- preflight 预检（2.8）----
    // 在绑定前检查文档里每个画布的 origin / channels 可满足性，返回问题清单
    //（空 = 全部可满足）。applyDocumentSpec 每次恢复前调用并把问题写进诊断；
    // 也可在恢复前独立调用（脚本/MCP 先验证再应用）。
    //  · origin 缺失：画布留空态、挂 pending 等晚到——不算失败，但要报告；
    //  · channel 缺失：源在但列对不上（换表头/协议变更）——绑定会静默跳过，
    //    preflight 提前暴露。
    static QStringList preflightDocumentSpec(const plot::PlotDocumentSpec &spec,
                                             const dataset::SourceHub &sources,
                                             const dataset::DatasetHub &datasets);

private:
    struct Cell
    {
        AxesView *canvas = nullptr;
        ChannelView *view = nullptr;
        InstrumentView *instrument = nullptr;
        QStackedWidget *stack = nullptr;
        // 每格的外框：竖排 [标题条, 画布]，标题条是拖动把手与按钮区。
        QWidget *frame = nullptr;
        // 标题条：格名、拆分/最大化/书签/AB/关闭，并负责把鼠标事件转给 ADS 标题栏。
        AxesToolbar *bar = nullptr;
        // 这一格在 ADS 里的停靠面板。块的"壳"会在重排时重建，frame 始终复用（曲线不丢）。
        DockedAxes *dockWidget = nullptr;
    };

    // 一个绘图页：滚动容器 + 页内 dock 管理器 + 该页的 cell。
    // cells 在这里持久保存，切页时与 m_cells 交换——这样所有既有代码
    // 继续操作"当前页的 cell 列表"，不必到处带页索引。
    struct Page
    {
        QWidget *host = nullptr;           // 绘图页本体（dock manager 的父）
        PlotLayout *dock = nullptr;      // 页内布局容器：每格一个停靠面板，摆格/等分都归它
        QVector<Cell> cells;
        int rows = 1;
        int cols = 1;
        bool gridMode = true;            // 手动拆分后置 false：重排不再抹平拆分结果
        QString title;
    };

    // —— 页操作 ——
    int addPage(const QString &title = QString());
    bool duplicatePage();             // 复制当前页（含页内全部画布与曲线）
    bool removePage(int index);       // 关闭页（至少保留一页）
    void switchPage(int index);
    void syncPage();                  // m_cells/m_rows/m_cols 写回当前页

    // —— cell 操作（view action 处理器调用）——
    int addCell();                    // 在当前页末尾追加一个画布并激活
    bool removeAt(int index);         // 删除 cell（至少保留一个）
    bool split(bool horizontal);      // 拆分活动 cell：水平=左右，垂直=上下（格内再拆，不动其它格）
    bool duplicateActive();           // 复制活动画布（含曲线与样式）
    bool setGrid(int rows, int cols); // 当前页重排为 rows×cols 网格
    bool setCellType(int index, const QString &kind); // 指定格切换曲线/仪表类型
    void toggleFullscreen();          // 单画布全屏/退出

    // 按 rows×cols 重建当前页的停靠布局（cell 内容复用，不重建、不丢数据）。
    void rebuildPageLayout();
    // 剔空 + 按容量算行后的重排：先丢掉没有任何曲线的格，再按「行 = max(rows, ceil(N/cols))」
    // 排满，不足的补空画布。返回实际使用的行数。
    int reflowGrid(int rows, int cols);
    // 装配一个 cell（画布 + 仪表 + 堆叠 + 信号），调用方负责放进布局。
    Cell makeCell(QWidget *parentForStack);
    // 停靠面板 → 当前页 cell 下标（-1 = 不属于本页）。
    int indexOfDock(const DockedAxes *dockWidget) const;
    // 把 cell 的 stack 从停靠面板里摘出（重建前必须先摘，否则删面板会连着删控件）。
    void detachStacks();

    // —— 跨画布曲线移动——
    // 按画布身份找 cell：拖拽的源画布只在 MIME 里留 id，这里是唯一的翻译层。
    AxesView *canvasById(int id) const;
    ChannelView *viewOfCanvas(const AxesView *canvas) const;
    int m_canvasSeq = 0;              // 画布 id 分配器（不持久化，进程内唯一即可）

    // —— 每格标题条（AxesToolbar 的显示与定位）——
    void showHoverBar(int index);
    void hideHoverBars();
    int indexOfBar(const QObject *bar) const;
    // 同一停靠区里出现多格（合并成页签）时，收起各格标题条，让位给 ADS 页签条。
    void refreshTabSuppression();

    // 集中注册 view action（唯一注册点，路由到当前页的 active cell）。
    // 各域实现分在 commands/，这里只保留入口。
    void registerViewActions();
    void registerPageActions();
    void registerCanvasActions();
    void registerSeriesActions();
    void registerMarkupActions();
    void registerPlaybackActions();
    // 注册曲线/轴/画布的属性 provider 与 setter（属性面板按 kind 路由到这里）。
    void registerPropertyProviders();
    // 连接一个画布的选中请求信号 -> AppContext.selection。
    void connectCanvasSignals(AxesView *canvas);

    void onActiveChannelChanged(int datasetId, const QString &table, const QString &column);
    // 播放头同步：引擎时间 → 所有 cell 画布的竖线。
    void onPlayheadChanged(double t);
    // 活动数据集变化时把时间范围喂给播放引擎（回放区间 = 数据首末时间）。
    void syncPlaybackRange(int datasetId);

    // —— 晚到绑定（pending binder）——
    // 一条挂起的绑定意图：来源标识、要接的通道、存盘颜色、页/格位置与存盘视口。
    // origin 找回源后按 channels 逐一接上（显式色直达），再 restoreViewState
    // 回放视口（对标 PJ4 applySavedViewportOrZoom：bind 后重放，防 auto-fit 抢走）。
    struct PendingBind
    {
        int page = 0;
        int cell = 0;
        QString origin;
        QStringList channels;
        QHash<QString, QColor> colorByChannel;
        QJsonObject state; // 存盘视口等画布状态（绑定后回放）
        QString title;     // 诊断信息用画布名
    };
    // 挂起队列 + 尝试接回（sourceAdded/datasetAdded 信号触发）。
    void tryBindPending();
    // 释放全部 pending 条目持有的 demand 引用（换文档/析构时调用）。
    void dropPendingBinds();
    // 布局先到、数据晚到的挂起绑定条目（见 applyDocumentSpec 内的挂起分支）。
    QVector<PendingBind> m_pendingBinds;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    context::AppContext &m_appCtx;
    intent::IIntentExecutor &m_executor;

    TabbedFigure *m_tabs = nullptr;       // 页签栏（末尾＋与改名归它）
    QVector<Page *> m_pages;
    int m_activePage = 0;

    QVector<Cell> m_cells;            // 当前页的 cell（与 m_pages[m_activePage]->cells 交换）
    int m_active = 0;
    int m_fullscreenCell = -1;        // >=0 表示某 cell 处于全屏（其它 cell 面板被隐藏）
    // 全屏时被隐藏的其它面板：退出时只恢复这一批，用户自己关掉的不会被重新打开。
    QVector<QPointer<DockedAxes>> m_fullscreenHidden;
    int m_rows = 1;
    int m_cols = 1;

    PlaybackController *m_playback = nullptr;

    // 悬浮工具栏的自动隐藏计时（鼠标停止移动一段时间后收起，避免遮挡曲线）。
    QTimer *m_hoverTimer = nullptr;

    friend QWidget *createPlotWorkspace(context::AppContext &appCtx,
                                          intent::IIntentExecutor &executor, QWidget *parent);
};

// 工厂：注册进部件表，供 PlotModule::onAttach 申请（"plot-workspace"）。
QWidget *createPlotWorkspace(context::AppContext &appCtx, intent::IIntentExecutor &executor,
                               QWidget *parent = nullptr);

} // namespace nextpilot::plotscope::gui
