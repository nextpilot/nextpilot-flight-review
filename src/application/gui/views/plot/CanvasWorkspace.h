#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include <QPointer>
#include <QWidget>

#include <QVector>

class QScrollArea;
class QLabel;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QEvent;

namespace ads
{
class CDockManager;
class CDockWidget;
} // namespace ads

namespace nextpilot::plotscope::gui
{

class ChannelView;
class InstrumentView;
class PlotCanvas;
class PlaybackEngine;
class TimelineWidget;

// 画布工作区：中央区域的组合容器。
//
// 层次：Tab（绘图页）→ 页内 QSplitter 树（m×n 网格 / 手动拆分）→ Cell（一块画布）
//
// 为什么要这一层（而不是让外壳直接摆一个 PlotCanvas）：
//   - page.new / 拆分 / 网格排布是"每页 N 个画布"，一个画布承载不了；
//   - 页签是**工作视图**的单位：同一份数据可以按不同页看不同角度（总览/局部/仪表），
//     切页不重新载入数据，也不复制数据；
//   - view action（plot.*）若由每个画布各自注册，后建的会劫持先建的（QHash 覆盖）——
//     必须由**唯一仲裁者**集中注册一次，路由到当前活动 cell；
//   - 活动通道（数据集树双击/拖拽）只追加到活动 cell，符合"当前画布"语义。
//
// 分工：本类只做布局与路由（QWidget 组合），取数仍走 ChannelView→意图层，
// 绘制仍是 PlotCanvas→fastplot。多画布不复制数据、不新增数据概念。
class CanvasWorkspace : public QWidget
{
    Q_OBJECT
public:
    CanvasWorkspace(context::AppContext &appCtx, intent::IIntentExecutor &executor,
                    QWidget *parent = nullptr);
    ~CanvasWorkspace() override;

    // 当前页内的画布格数（老 cellCount 的语义收窄到"当前页"）。
    int cellCount() const;
    int pageCount() const;
    int activeIndex() const;
    // 当前活动画布（无画布时返回 nullptr）。仅供视图内部路由，不交给外壳。
    PlotCanvas *activeCanvas() const;
    ChannelView *activeChannelView() const;

    // 程序化浮出活动格的悬浮工具栏（命令面板/截图/自动化验收用）。
    // autoHide=false 时不做自动收起，留给人看或截图。
    void flashToolbar(bool autoHide = true);

    // 当前页网格规格（行×列），截图/状态显示用。
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    // 设为活动 cell（点击/程序指定）。view action 处理器也用；测试需显式定位画布。
    void setActive(int index);

private:
    struct Cell
    {
        PlotCanvas *canvas = nullptr;
        ChannelView *view = nullptr;
        InstrumentView *instrument = nullptr;
        QStackedWidget *stack = nullptr;
        // 每格的外框：竖排 [标题条, 画布]，标题条就是拖动把手与按钮区（PJ4 的 DockToolbar 形态）。
        QWidget *frame = nullptr;
        QWidget *hoverBar = nullptr;   // 标题条（左右/上下拆分、最大化、书签、AB、关闭），frame 的第一行
        QLabel *titleLabel = nullptr;  // 标题条上的格名，双击可改
        // 这一格在 ADS 里的停靠面板。块的"壳"会在重排时重建，frame 始终复用（曲线不丢）。
        ads::CDockWidget *dockWidget = nullptr;
    };

    // 一个绘图页：滚动容器 + 页内 dock 管理器 + 该页的 cell。
    // cells 在这里持久保存，切页时与 m_cells 交换——这样所有既有代码
    // 继续操作"当前页的 cell 列表"，不必到处带页索引。
    struct Page
    {
        QWidget *host = nullptr;           // 绘图页本体（dock manager 的父）
        ads::CDockManager *dock = nullptr; // 页内布局容器：每格一个停靠面板
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
    // 让页内所有分隔条恢复等分（点 m×n 即回到严格等分状态）。
    void equalizeSplitters();
    // 装配一个 cell（画布 + 仪表 + 堆叠 + 信号），调用方负责放进布局。
    Cell makeCell(QWidget *parentForStack);
    // 把 cell 装进停靠面板。side: 0=中心格 1=右侧 2=下方；intoContainer 为真时相对整页容器插入（用于换行）。
    ads::CDockWidget *makeDockForCell(Cell &cell, int side, ads::CDockWidget *relative = nullptr,
                                      bool intoContainer = false);
    // 停靠面板 → 当前页 cell 下标（-1 = 不属于本页）。
    int indexOfDock(const ads::CDockWidget *dockWidget) const;
    // 把 cell 的 stack 从停靠面板里摘出（重建前必须先摘，否则删面板会连着删控件）。
    void detachStacks();

    // —— 跨画布曲线移动——
    // 按画布身份找 cell：拖拽的源画布只在 MIME 里留 id，这里是唯一的翻译层。
    PlotCanvas *canvasById(int id) const;
    ChannelView *viewOfCanvas(const PlotCanvas *canvas) const;
    int m_canvasSeq = 0;              // 画布 id 分配器（不持久化，进程内唯一即可）

    // —— 每格标题条（对齐老 PlottingCanvas 的 m_hoverBar）——
    QWidget *makeHoverBar(Cell &cell, QWidget *parentStack);
    void layoutHoverBar(Cell &cell);
    void showHoverBar(int index);
    void hideHoverBars();
    // 亮/暗切换一格标题条：暗只是不显示，仍占位，进格出现时布局不跳。
    void setCellBarShown(Cell &cell, bool shown);
    int indexOfHoverBar(const QObject *bar) const;

    // 集中注册 view action（唯一注册点，路由到当前页的 active cell）。
    void registerViewActions();
    // 注册曲线/轴/画布的属性 provider 与 setter（属性面板按 kind 路由到这里）。
    void registerPropertyProviders();
    // 连接一个画布的选中请求信号 -> AppContext.selection。
    void connectCanvasSignals(PlotCanvas *canvas);

    void onActiveChannelChanged(int datasetId, const QString &table, const QString &column);
    // 播放头同步：引擎时间 → 所有 cell 画布的竖线。
    void onPlayheadChanged(double t);
    // 活动数据集变化时把时间范围喂给播放引擎（回放区间 = 数据首末时间）。
    void syncPlaybackRange(int datasetId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    context::AppContext &m_appCtx;
    intent::IIntentExecutor &m_executor;

    QTabWidget *m_tabs = nullptr;     // 页签容器（可关闭/可移动）
    QVector<Page *> m_pages;
    int m_activePage = 0;

    QVector<Cell> m_cells;            // 当前页的 cell（与 m_pages[m_activePage]->cells 交换）
    int m_active = 0;
    int m_fullscreenCell = -1;        // >=0 表示某 cell 处于全屏（其它 cell 面板被隐藏）
    // 全屏时被隐藏的其它面板：退出时只恢复这一批，用户自己关掉的不会被重新打开。
    QVector<QPointer<ads::CDockWidget>> m_fullscreenHidden;
    int m_rows = 1;
    int m_cols = 1;
    // 重建停靠树期间为真：此时 ADS 的 dockWidgetRemoved 是"我们在拆旧树"，不该当成用户关格。
    bool m_rebuildingDock = false;

    PlaybackEngine *m_playback = nullptr;
    TimelineWidget *m_timeline = nullptr;

    // 悬浮工具栏的自动隐藏计时（鼠标停止移动一段时间后收起，避免遮挡曲线）。
    QTimer *m_hoverTimer = nullptr;

    friend QWidget *createCanvasWorkspace(context::AppContext &appCtx,
                                          intent::IIntentExecutor &executor, QWidget *parent);
};

// 工厂：注册进部件表，供 PlotModule::onAttach 申请（"canvas-workspace"）。
QWidget *createCanvasWorkspace(context::AppContext &appCtx, intent::IIntentExecutor &executor,
                               QWidget *parent = nullptr);

} // namespace nextpilot::plotscope::gui
