#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include <QWidget>

#include <QVector>

class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QEvent;

namespace nextpilot::plotscope::gui
{

class ChannelView;
class InstrumentView;
class PlotCanvas;
class PlaybackEngine;
class TimelineWidget;

// 画布工作区：中央区域的组合容器。
//
// 层次对齐老代码（main 分支的 PlottingTabs + PlottingLayout + PlottingCanvas）：
//   Tab（绘图页）→ 页内 QSplitter 树（m×n 网格 / 手动拆分）→ Cell（一块画布）
//
// 为什么要这一层（而不是让外壳直接摆一个 PlotCanvas）：
//   - 老代码 page.new / 拆分 / 网格排布是"每页 N 个画布"，一个画布承载不了；
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
        QWidget *hoverBar = nullptr;   // 悬浮小工具栏（左右/上下拆分、最大化、关闭），stack 子控件
    };

    // 一个绘图页：滚动容器 + 页内 splitter 树 + 该页的 cell。
    // cells 在这里持久保存，切页时与 m_cells 交换——这样所有既有代码
    // 继续操作"当前页的 cell 列表"，不必到处带页索引。
    struct Page
    {
        QWidget *host = nullptr;      // QScrollArea 的内容控件（splitter 树的父）
        QScrollArea *scroll = nullptr;
        QSplitter *root = nullptr;    // 垂直：一行一个子 splitter
        QVector<Cell> cells;
        int rows = 1;
        int cols = 1;
        bool gridMode = true;         // 手动拆分后置 false：重排不再抹平拆分结果
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
    bool split(bool horizontal);      // 拆分活动 cell：水平=左右，垂直=上下
    bool duplicateActive();           // 复制活动画布（含曲线与样式）
    bool setGrid(int rows, int cols); // 当前页重排为 rows×cols 网格
    bool setCellType(int index, const QString &kind); // 指定格切换曲线/仪表类型
    void toggleFullscreen();          // 单画布全屏/退出

    // 按 rows×cols 重建当前页的 splitter 树（cell 控件复用，不重建、不丢数据）。
    void rebuildPageLayout();
    // 装配一个 cell（画布 + 仪表 + 堆叠 + 信号），调用方负责放进布局。
    Cell makeCell(QWidget *parentForStack);
    // 把 cell 的 stack 从布局摘出（重建树前必须先摘，否则 delete splitter 会连着删控件）。
    void detachStacks();

    // —— 每格悬浮工具栏（对齐老 PlottingCanvas 的 m_hoverBar）——
    QWidget *makeHoverBar(Cell &cell, QWidget *parentStack);
    void layoutHoverBar(Cell &cell);
    void showHoverBar(int index);
    void hideHoverBars();
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
    int m_fullscreenCell = -1;        // >=0 表示某 cell 处于全屏（其它 cell 隐藏）
    int m_rows = 1;
    int m_cols = 1;

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
