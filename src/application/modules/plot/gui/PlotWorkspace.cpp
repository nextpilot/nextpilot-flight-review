#include "PlotWorkspace.h"

#include <QDateTime>

#include "AppContext.h"
#include "chrome/AxesToolbar.h"
#include "layout/TabbedFigure.h"
#include "layout/PlotLayout.h"
// 文档 spec 结构在模块层；与上面视图层的 layout/PlotLayout.h 同名不同物，
// 靠路径区分：本文件读写的是"可存盘的布局声明"，不是 dock 布局容器。
#include "PlotDocument.h"
#include "layout/DockedAxes.h"
#include "ChannelView.h"
#include "GridPickPopup.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryHub.h"
#include "AxesView.h"
#include "WorkspaceHistory.h"
#include "playback/PlaybackController.h"
#include "PlaybackEngine.h"
#include "DatasetHub.h"
#include "Dataset.h"
#include "SourceHub.h"
#include "PropertySchema.h"
#include "Result.h"
#include "ErrorCodes.h"
#include <cmath>
#include "Transform.h"
#include "TransformEditorDialog.h"
#include <fastplot/Axes.h>
#include <fastplot/Panner.h>
#include <fastplot/Zoomer.h>

#include <algorithm>
#include <functional>
#include <limits>

// 布局层：每格一个 ADS 停靠面板（拖动分屏 / 拖到中心合并 / 拖出浮动 / 拖回停靠全由 ADS 提供）。
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockContainerWidget.h"
#include "DockManager.h"
#include "DockSplitter.h"
#include "DockWidget.h"

#include <QEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QToolTip>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QSet>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStringList>

#include <algorithm>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui
{

namespace
{
// 曲线类图型：在 AxesView 内切（都是"曲线"模型，走 LOD 折线）。
bool isCurveKind(const QString &kind)
{
    return kind == QStringLiteral("timeseries") || kind == QStringLiteral("scatter")
           || kind == QStringLiteral("xy");
}

// 专用图型：fastplot 的 Instrument widget（仪表/地图/3D/柱/直方/频谱）。
bool isInstrumentKind(const QString &kind)
{
    static const QStringList k = {
        QStringLiteral("gauge"),      QStringLiteral("compass"),
        QStringLiteral("horizon"),    QStringLiteral("traj3d"),
        QStringLiteral("map"),        QStringLiteral("waterfall"),
        QStringLiteral("bar"),        QStringLiteral("histogram"),
        QStringLiteral("fft"),
    };
    return k.contains(kind);
}

// gallery 的 map/horizon 映射到 InstrumentView 的真实 kind。
QString toInstrumentKind(const QString &kind)
{
    if (kind == QStringLiteral("map"))
        return QStringLiteral("track");
    if (kind == QStringLiteral("horizon"))
        return QStringLiteral("attitude");
    return kind;
}
} // namespace

PlotWorkspace::PlotWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_executor(executor)
{
    // 游标状态栏尾沿补发（10Hz 节流在 plot 域，见 pushCursorStatus）。
    m_cursorFlush.setSingleShot(true);
    QObject::connect(&m_cursorFlush, &QTimer::timeout, this, [this]() {
        if (!m_cursorPendingValid)
            return;
        m_cursorPendingValid = false;
        m_cursorStatusEmitMs = QDateTime::currentMSecsSinceEpoch();
        emitCursorStatus(m_cursorPending);
    });

    // 全工作区快照 undo（第3仗 3b）：最先创建——结构方法与信号都会打进动作流。
    m_history = new WorkspaceHistory(this, this);

    // 外层纵向布局：网格画布区 + 底部时间条。
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 页签栏：一页 = 一套绘图布局；末尾「＋」与改名都归 TabbedFigure，
    // 这里只按真实页索引接信号。
    m_tabs = new TabbedFigure(this);
    outer->addWidget(m_tabs, 1);

    QObject::connect(m_tabs, &TabbedFigure::pageSelected, this, [this](int index) { switchPage(index); });
    QObject::connect(m_tabs, &TabbedFigure::pageCloseRequested, this,
                     [this](int index) { removePage(index); });
    QObject::connect(m_tabs, &TabbedFigure::titleEdited, this,
                     [this](int index, const QString &title) {
                         if (index >= 0 && index < m_pages.size())
                             m_pages.at(index)->title = title;
                     });
    // 点「＋」：新建一页并切过去，页内先给一格空画布。
    QObject::connect(m_tabs, &TabbedFigure::addPageRequested, this, [this]() {
        const int created = addPage();
        m_tabs->setCurrentPage(created);
        if (m_cells.isEmpty())
            addCell();
        m_rows = 1;
        m_cols = 1;
    });

    // 回放控制：引擎 + 底部时间条 + 播放头广播；这里只把它装进布局。
    m_playback = new PlaybackController(this);
    outer->addWidget(m_playback->timelineWidget());
    QObject::connect(m_playback, &PlaybackController::playheadChanged, this,
                     &PlotWorkspace::onPlayheadChanged);
    // 回放状态 → 状态栏 `playback.state`：引擎 playingChanged 驱动，不在壳里猜业务键。
    QObject::connect(m_playback->engine(), &PlaybackEngine::playingChanged, this,
                     [this](bool playing) { pushPlaybackStatus(playing); });

    // 源级时间 offset 变化（第二梯队 #204 多源时间对齐）：统一刷新所有画布。
    // offset 是宿主态配方，画布 LOO/原始数据不动，只重推 x 映射。
    QObject::connect(&m_appCtx, &AppContext::sourceTimeOffsetChanged, this,
                     [this](int, double) {
                         for (const Cell &c : m_cells)
                             if (c.canvas)
                                 c.canvas->refreshSourceOffsets();
                     });

    // 默认一页，页内一个画布 cell。
    // insertTab 会把 current 留在原来的「＋」上，必须显式切回第 0 页。
    addPage(QStringLiteral("页 1"));
    m_tabs->setCurrentPage(0);
    addCell();
    m_rows = 1;
    m_cols = 1;

    // 悬浮工具栏的自动收起：鼠标离开这一格才隐藏，不让按钮常驻压住曲线。
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(1600);
    QObject::connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
        // 鼠标停在这一格内（画布或浮在其上的工具栏）就续期，不收起。
        const QPoint cursor = QCursor::pos();
        for (int i = 0; i < m_cells.size(); ++i)
        {
            QWidget *frame = m_cells.at(i).frame;
            if (frame && frame->isVisible() && frame->rect().contains(frame->mapFromGlobal(cursor)))
            {
                showHoverBar(i);
                return;
            }
        }
        hideHoverBars();
    });

    // 活动通道只追加到当前 cell（"当前画布"语义）。
    QObject::connect(&m_appCtx, &AppContext::activeChannelChanged, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         onActiveChannelChanged(datasetId, t, c);
                         syncPlaybackRange(datasetId);
                     });

    // 数据集拓扑变化：先重绑失效曲线（数据集替换后 key 里的 datasetId 失效），
    // 再尝试接回 pending 条目。两个信号都要挂——文件源是 sourceAdded（登记即带
    // datasetIds）；live 源是 connectLive 先登记源、解析器吐数据后才 addDataset，
    // 只有 datasetAdded 才能补上。
    QObject::connect(&m_appCtx.sources(), &dataset::SourceHub::sourceAdded,
                     this, [this](int) { onDatasetTopologyChanged(); });
    QObject::connect(&m_appCtx.sources(), &dataset::SourceHub::sourceChanged,
                     this, [this](int) { onDatasetTopologyChanged(); });
    QObject::connect(&m_appCtx.data(), &dataset::DatasetHub::datasetAdded,
                     this, [this](int) { onDatasetTopologyChanged(); });

    registerViewActions();
    registerPropertyProviders();

    // 文档作用域登记：本工作区是 PlotDocumentSpec 的唯一生产者与消费者，
    // 登记放这里而不是模块层——模块拿不到部件实例（面板由外壳按类型申请）。
    // 工作区可能先于 AppContext 销毁，回调统一经 QPointer 判空。
    QPointer<PlotWorkspace> self(this);
    context::PersistentStateEntry entry;
    entry.id = QStringLiteral("plot.document");
    entry.scope = context::PersistentStateEntry::Document;
    entry.version = 1;
    entry.bestEffort = true;
    entry.serialize = [self]() -> QJsonObject {
        if (!self)
            return {};
        return self->toDocumentSpec().toJson();
    };
    entry.restore = [self](const QJsonObject &doc) {
        if (!self)
            return;
        self->applyDocumentSpec(plot::PlotDocumentSpec::fromJson(doc), self->m_appCtx.sources());
    };
    m_appCtx.states().registerEntry(std::move(entry));

    // 会话视图态：回到哪一页、哪一格。只存两个索引——页与格的实质内容
    // 归 plot.document，这里再存一份会成为第二个真相来源。
    context::PersistentStateEntry canvasEntry;
    canvasEntry.id = QStringLiteral("canvas.session");
    canvasEntry.scope = context::PersistentStateEntry::Session;
    canvasEntry.version = 1;
    canvasEntry.bestEffort = true;
    canvasEntry.serialize = [self]() -> QJsonObject {
        if (!self)
            return {};
        QJsonObject o;
        o.insert(QStringLiteral("activePage"), self->m_activePage);
        o.insert(QStringLiteral("activeCell"), self->m_active);
        return o;
    };
    canvasEntry.restore = [self](const QJsonObject &doc) {
        if (!self)
            return;
        const int page = doc.value(QStringLiteral("activePage")).toInt(0);
        if (page >= 0 && page < self->m_pages.size())
        {
            if (self->m_tabs != nullptr)
                self->m_tabs->setCurrentPage(page);
            self->switchPage(page);
        }
        const int cell = doc.value(QStringLiteral("activeCell")).toInt(0);
        if (cell >= 0 && cell < self->m_cells.size())
            self->setActive(cell);
    };
    m_appCtx.states().registerEntry(std::move(canvasEntry));
}

PlotWorkspace::~PlotWorkspace()
{
    // pending 条目持有的 demand 引用要释放，否则 SourceHub 的需求计数泄漏。
    dropPendingBinds();
    // 页对象是裸指针（Page 里还持有 scroll/host 的父子关系），析构时显式释放。
    // cell 控件归 Qt 父子树管，随 scroll 一起销毁。
    qDeleteAll(m_pages);
    m_pages.clear();
}

int PlotWorkspace::cellCount() const
{
    return m_cells.size();
}

int PlotWorkspace::pageCount() const
{
    return m_pages.size();
}

int PlotWorkspace::activeIndex() const
{
    return m_active;
}

AxesView *PlotWorkspace::activeCanvas() const
{
    return m_cells.isEmpty() ? nullptr : m_cells.at(qBound(0, m_active, m_cells.size() - 1)).canvas;
}

ChannelView *PlotWorkspace::activeChannelView() const
{
    return m_cells.isEmpty() ? nullptr : m_cells.at(qBound(0, m_active, m_cells.size() - 1)).view;
}

// 装配一块画布 cell：画布 + 仪表 + 堆叠容器，并把所有信号接好。
// addCell / split / duplicateActive 共用它，避免三份重复的装配代码各自漂移。
PlotWorkspace::Cell PlotWorkspace::makeCell(QWidget *parentForStack)
{
    // 每格一个外框：竖排 [标题条, 画布堆叠]。停靠面板装的是这个外框，
    // 标题条因此固定在格子顶部，既是按钮区也是拖动把手（PJ4 DockToolbar 的形态）。
    auto *frame = new QWidget(parentForStack);
    frame->setObjectName(QStringLiteral("plotCellFrame"));
    auto *frameLay = new QVBoxLayout(frame);
    frameLay->setContentsMargins(0, 0, 0, 0);
    frameLay->setSpacing(0);

    auto *stack = new QStackedWidget(frame);
    auto *canvas = new AxesView(stack);
    canvas->setCanvasId(++m_canvasSeq);
    auto *view = new ChannelView(m_appCtx.transforms(), m_executor, canvas, canvas);

    auto *instrument = new InstrumentView(m_executor, stack);
    instrument->setAppContext(&m_appCtx);

    // 游标变化 → 状态栏 `plot.axes.cursor`（只推当前活动格，避免多格互相覆盖）。
    QObject::connect(canvas, &AxesView::cursorTimeChanged, this,
                     [this, canvas, stack](double t) {
                         if (stack->currentWidget() == canvas)
                             pushCursorStatus(t);
                     });
    stack->addWidget(canvas);
    stack->addWidget(instrument);
    stack->setCurrentWidget(canvas);
    // 点击任意 cell 设为活动（子控件安装事件过滤器）。
    stack->installEventFilter(this);
    // 列节点拖到画布 → 画布只解码 MIME 发信号 → 这里切活动通道
    // （与数据集树双击同一路径，activity 路由回当前 cell）。
    QObject::connect(canvas, &AxesView::channelDropped, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         m_appCtx.setActiveChannel(datasetId, t, c);
                     });
    // 曲线从一个画布拖到另一个画布：目标是"接收方"，源按 MIME 里的 canvasId 回找。
    // 先让目标加上，成功后才删源——目标失败（比如数据集已卸载）时源不受影响，
    // 也就不会出现"两边都没有"的半截状态。
    QObject::connect(canvas, &AxesView::seriesTransferRequested, this,
                     [this](int srcCanvasId, const QString &key, int datasetId,
                            const QString &table, const QString &column, const QString &title) {
                         auto *targetCanvas = qobject_cast<AxesView *>(sender());
                         if (!targetCanvas || targetCanvas->canvasId() == srcCanvasId)
                             return;
                         ChannelView *targetView = viewOfCanvas(targetCanvas);
                         AxesView *srcCanvas = canvasById(srcCanvasId);
                         ChannelView *srcView = viewOfCanvas(srcCanvas);
                         if (!targetView || !srcView)
                             return; // 源不在当前页（跨页移动未支持）：拒绝，源保持原样
                         if (!targetView->addChannel(datasetId, table, column, nullptr, title))
                             return;
                         // 移动不是复制：目标成功后才摘源。
                         srcView->removeChannel(datasetId, table, column);
                         Q_UNUSED(key);
                     });
    // Ctrl+拖通道到画布 = XY 建图手势（第3仗 3c / F-1）：首曲线作 X、落下通道作 Y。
    QObject::connect(canvas, &AxesView::xyDropRequested, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         createXyCanvasFor(datasetId, t, c);
                     });
    // 画布选中请求 -> 统一 selection（属性面板据此刷新）。
    connectCanvasSignals(canvas);
    // 全局 undo 动作流（第3仗 3b）：曲线增删/样式/挂轴（store changed 转发）与
    // 显隐（单一出口广播）。结构操作（拆分/页操作）在各自方法起始处通知。
    QObject::connect(canvas, &AxesView::seriesMutated, this, [this] { notifyHistoryMutation(); });
    QObject::connect(canvas, &AxesView::seriesVisibilityChanged, this,
                     [this](const QString &, bool) { notifyHistoryMutation(); });
    // 时间轴刻度按设置初始化：新格沿用当前全局偏好。
    canvas->setTimeFormatMode(m_appCtx.settings()
                                  .scopedValue(nextpilot::plotscope::context::SettingsScope::App,
                                               QStringLiteral("plot.time.format"), QStringLiteral("relative"))
                                  .toString());

    Cell cell;
    cell.canvas = canvas;
    cell.view = view;
    cell.instrument = instrument;
    cell.stack = stack;
    cell.frame = frame;
    // 每格的标题条：常显在格子顶部，按钮直接作用在"这一格"。
    cell.bar = new AxesToolbar(frame);
    frameLay->addWidget(cell.bar);
    frameLay->addWidget(stack, 1);
    // 初始为"收起"态：鼠标进到这一格才亮出来。
    cell.bar->setShown(false);

    // 按钮只发信号，作用于哪一格由标题条反查——每格不再各连一套定位逻辑。
    AxesToolbar *bar = cell.bar;
    QObject::connect(bar, &AxesToolbar::activated, this, [this, bar]() {
        const int index = indexOfBar(bar);
        if (index >= 0)
            setActive(index);
    });
    QObject::connect(bar, &AxesToolbar::splitClicked, this, [this, bar](bool horizontal) {
        const int index = indexOfBar(bar);
        if (index < 0)
            return;
        setActive(index);
        split(horizontal);
        showHoverBar(m_active);
    });
    QObject::connect(bar, &AxesToolbar::maximizeClicked, this, [this, bar]() {
        const int index = indexOfBar(bar);
        if (index < 0)
            return;
        setActive(index);
        toggleFullscreen();
        showHoverBar(m_active);
    });
    // 书签：以当前游标时刻为准；没有游标就取可见范围中点（总有明确落点）。
    QObject::connect(bar, &AxesToolbar::bookmarkClicked, this, [this, bar]() {
        const int index = indexOfBar(bar);
        if (index < 0 || !m_cells[index].canvas)
            return;
        AxesView *target = m_cells[index].canvas;
        double t = 0.0;
        if (target->cursorActiveForTest())
            t = target->cursorTime();
        else
        {
            double t0 = 0.0;
            double t1 = 0.0;
            if (target->visibleTimeRange(&t0, &t1))
                t = (t0 + t1) / 2.0;
        }
        target->addBookmark(t, QStringLiteral("书签"));
    });
    // A/B：与画布右键/Ctrl+左键同一条实现（以游标为中心生成区间）。
    QObject::connect(bar, &AxesToolbar::abClicked, this, [this, bar]() {
        const int index = indexOfBar(bar);
        if (index >= 0 && m_cells[index].canvas)
            m_cells[index].canvas->toggleAB();
    });
    QObject::connect(bar, &AxesToolbar::closeClicked, this, [this, bar]() {
        const int index = indexOfBar(bar);
        if (index >= 0)
            removeAt(index);
    });
    // 图例从上往下让出标题条的高度，曲线不与标题条贴边。
    if (auto *ax = canvas->plotForTest())
        ax->setLegendTopInset(28.0);
    // 鼠标在画布/仪表上移动时浮出工具栏（画布内部收事件，过滤器必须装在画布上）。
    canvas->installEventFilter(this);
    instrument->installEventFilter(this);
    // fastplot 的 Picker 吃掉了鼠标事件，父控件收不到 MouseMove；
    // 走画布自己的 pointerHovered 信号（每帧悬停回调）来浮出本格工具栏。
    QObject::connect(canvas, &AxesView::pointerHovered, this, [this, canvas]() {
        for (int i = 0; i < m_cells.size(); ++i)
        {
            if (m_cells.at(i).canvas == canvas)
            {
                showHoverBar(i);
                return;
            }
        }
    });
    return cell;
}

int PlotWorkspace::addCell()
{
    notifyHistoryMutation();
    m_cells.append(makeCell(nullptr));
    m_active = m_cells.size() - 1;
    // 加到当前页末尾：网格模式下按列数补位（新格落在下一行首列）。
    m_cols = std::max(1, m_cols);
    m_rows = (m_cells.size() + m_cols - 1) / m_cols;
    rebuildPageLayout();
    syncPage();
    return m_active;
}

void PlotWorkspace::flashToolbar(bool autoHide)
{
    showHoverBar(m_active);
    if (!autoHide && m_hoverTimer)
        m_hoverTimer->stop();
}

int PlotWorkspace::indexOfBar(const QObject *bar) const
{
    if (!bar)
        return -1;
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells.at(i).bar == bar)
            return i;
    return -1;
}

void PlotWorkspace::showHoverBar(int index)
{
    // 只有指针所在的这一格亮出标题条；其它格回到"占位不显示"。
    if (index < 0 || index >= m_cells.size())
        return;
    setActive(index);
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells[i].bar)
            m_cells[i].bar->setShown(i == index);
    if (m_hoverTimer)
        m_hoverTimer->start();
}

void PlotWorkspace::hideHoverBars()
{
    // 鼠标停下/离开一格后收起：所有格回到占位状态（控件仍在，不跳布局）。
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells[i].bar)
            m_cells[i].bar->setShown(false);
}

void PlotWorkspace::refreshTabSuppression()
{
    // 停靠区里合并了多格时，ADS 的页签条接管格名与关闭，各格标题条让位。
    for (Cell &cell : m_cells)
    {
        if (!cell.bar)
            continue;
        const bool merged = cell.dockWidget && cell.dockWidget->dockAreaWidget()
                             && cell.dockWidget->dockAreaWidget()->dockWidgets().size() > 1;
        cell.bar->setSuppressed(merged);
    }
}

bool PlotWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // 这一格内的对象（外框 / 堆叠 / 画布 / 仪表 / 标题条）都被装了过滤器：统一映射回 cell 索引。
    int cellIndex = -1;
    for (int i = 0; i < m_cells.size(); ++i)
    {
        const Cell &c = m_cells.at(i);
        if (watched == c.frame || watched == c.stack || watched == c.canvas || watched == c.instrument)
        {
            cellIndex = i;
            break;
        }
    }

    if (cellIndex >= 0)
    {
        // 标题条自有事件处理（见 AxesToolbar）：这里只回应格内其余区域的进出与按下。
        if (type == QEvent::MouseButtonPress)
        {
            setActive(cellIndex);
            showHoverBar(cellIndex);
        }
        else if (type == QEvent::Enter)
        {
            showHoverBar(cellIndex);      // 指针进到这一格 → 亮出它的标题条（离开后由定时器收起）
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PlotWorkspace::setActive(int index)
{
    if (index < 0 || index >= m_cells.size() || m_active == index)
        return;
    m_active = index;
}

void PlotWorkspace::pushCursorStatus(double t)
{
    // 10Hz 窗口（D2：游标节流归 plot 域，壳与 hub 不认识这个业务键）。
    // 窗口内只**暂存最新值**，由 m_cursorFlush 在窗口结束后补发一次尾沿，
    // 所以连续拖动停手后状态栏一定落到最终位置，不会停在中途值。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 since = now - m_cursorStatusEmitMs;
    if (since >= 100)
    {
        m_cursorStatusEmitMs = now;
        m_cursorPendingValid = false;
        emitCursorStatus(t);
        return;
    }
    m_cursorPending = t;
    m_cursorPendingValid = true;
    if (!m_cursorFlush.isActive())
        m_cursorFlush.start(int(100 - since) + 1); // 窗口一结束就补发
}

void PlotWorkspace::emitCursorStatus(double t)
{
    m_appCtx.status().updateStatus(QStringLiteral("plot.axes.cursor"),
                                   QStringLiteral("游标: %1 s").arg(t, 0, 'f', 2));
}

void PlotWorkspace::pushPlaybackStatus(bool playing)
{
    // 引擎只区分播放/暂停两态；初始"停止"由 spec.text 给初值，这里不回写。
    m_appCtx.status().updateStatus(QStringLiteral("playback.state"),
                                   playing ? QStringLiteral("播放中")
                                           : QStringLiteral("已暂停"));
}

AxesView *PlotWorkspace::canvasById(int id) const
{
    if (id < 0)
        return nullptr;
    for (const Cell &c : m_cells)
        if (c.canvas && c.canvas->canvasId() == id)
            return c.canvas;
    return nullptr;
}

ChannelView *PlotWorkspace::viewOfCanvas(const AxesView *canvas) const
{
    if (!canvas)
        return nullptr;
    for (const Cell &c : m_cells)
        if (c.canvas == canvas)
            return c.view;
    return nullptr;
}

void PlotWorkspace::detachStacks()
{
    // 重建停靠树前必须先把每格的外框移出去：删面板会删掉它持有的子控件，
    // 而外框（含标题条与画布）归本类所有（还要复用），不能被连带删除。
    for (Cell &c : m_cells)
    {
        if (!c.frame)
            continue;
        if (c.dockWidget)
        {
            // 把外框从面板里摘出来（面板随后销毁不会再连带删它）。
            if (QWidget *taken = c.dockWidget->takeCellWidget())
            {
                taken->setParent(nullptr);
                taken->hide();
            }
        }
        else
        {
            c.frame->setParent(nullptr);
            c.frame->hide();
        }
    }
}

int PlotWorkspace::indexOfDock(const DockedAxes *dockWidget) const
{
    if (!dockWidget)
        return -1;
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells.at(i).dockWidget == dockWidget)
            return i;
    return -1;
}

void PlotWorkspace::rebuildPageLayout()
{
    if (m_pages.isEmpty())
        return;
    Page &page = *m_pages[m_activePage];
    if (!page.host)
        return;

    // 重排会重建面板：全屏时被隐藏的其它面板先放回来，否则它们的隐藏记录就悬空了。
    for (const QPointer<DockedAxes> &dockWidget : m_fullscreenHidden)
        if (dockWidget)
            dockWidget->toggleView(true);
    m_fullscreenHidden.clear();
    m_fullscreenCell = -1;

    detachStacks();
    // 页内格数决定"删除此画布"是否可点：只剩一格时删无可删，
    // 菜单项直接置灰，别让用户点了才弹提示（每次重排后同步一次）。
    for (const Cell &c : m_cells)
        if (c.canvas)
            c.canvas->setCanvasRemovable(m_cells.size() > 1);

    // 拆掉旧容器（此时里面已经没有 cell 内容）。
    if (page.dock)
    {
        delete page.dock;
        page.dock = nullptr;
    }
    for (Cell &c : m_cells)
        c.dockWidget = nullptr;

    page.dock = new PlotLayout(page.host);
    page.dock->setRebuilding(true);

    // 合并/拆出页签后重算标题条的去留：页签条接管时各格标题条先让位。
    QObject::connect(page.dock, &PlotLayout::cellDockAdded, this, [this](DockedAxes *) {
        QTimer::singleShot(0, this, [this]() { refreshTabSuppression(); });
    });
    // 点/拖动某格即成为活动格（view action 都路由到它）。
    QObject::connect(page.dock, &PlotLayout::focusedDockWidgetChanged, this,
                     [this](ads::CDockWidget *, ads::CDockWidget *now) {
                         const int index = indexOfDock(qobject_cast<DockedAxes *>(now));
                         if (index >= 0)
                             setActive(index);
                     });
    // 用户点面板自带的关闭：把这一格一起收掉，别在 m_cells 里留空指针。
    // 重建旧树时容器自己也会发同样的信号（isRebuilding 期间一律忽略）。
    QObject::connect(page.dock, &PlotLayout::cellDockRemoved, this, [this](DockedAxes *removed) {
        auto *dock = qobject_cast<PlotLayout *>(sender());
        if (!removed || (dock && dock->isRebuilding()))
            return;
        const int index = indexOfDock(removed);
        if (index < 0)
            return;
        Cell cell = m_cells.takeAt(index);
        if (QWidget *taken = removed->takeCellWidget())
            taken->setParent(nullptr);
        removed->deleteLater();
        if (cell.frame)
        {
            cell.frame->setParent(nullptr);
            cell.frame->deleteLater();   // 外框带走标题条与画布
        }
        if (m_active >= m_cells.size())
            m_active = std::max(0, static_cast<int>(m_cells.size()) - 1);
        // 关到一格不剩时补回来；延迟一拍，别在 ADS 自己的删除调用栈里重建容器。
        if (m_cells.isEmpty())
            QTimer::singleShot(0, this, [this]() {
                if (m_cells.isEmpty())
                    addCell();
            });
        syncPage();
        refreshTabSuppression();
    });

    // 按 rows×cols 收集装配材料：格名按格序，用户改过名的保留自己的名字。
    QVector<CellSlot> cellSlots;
    cellSlots.reserve(m_cells.size());
    for (int i = 0; i < m_cells.size(); ++i)
    {
        Cell &cell = m_cells[i];
        const QString title = (cell.bar && cell.bar->title() != QStringLiteral("图"))
                                  ? cell.bar->title()
                                  : QStringLiteral("图 %1").arg(i + 1);
        if (cell.bar)
            cell.bar->setTitle(title);
        CellSlot slot;
        slot.title = title;
        slot.frame = cell.frame;
        slot.bar = cell.bar;
        cellSlots.append(slot);
    }

    const QVector<DockedAxes *> docks =
        page.dock->layoutGrid(cellSlots, std::max(1, m_rows), std::max(1, m_cols));
    for (int i = 0; i < docks.size() && i < m_cells.size(); ++i)
        m_cells[i].dockWidget = docks.at(i);

    page.gridMode = true;
    page.dock->setRebuilding(false);
    // 网格模式下每一级分隔器都等分：默认按 sizeHint 分配会让空白画布被挤成一条。
    page.dock->equalizeSplitters();
    refreshTabSuppression();
}

int PlotWorkspace::reflowGrid(int rows, int cols)
{
    rows = std::max(1, rows);
    cols = std::max(1, cols);

    // 1) 先剔掉没有任何曲线的空白格：空画布不该占一个网格位。
    QVector<int> keepIndices;
    keepIndices.reserve(m_cells.size());
    for (int i = 0; i < m_cells.size(); ++i)
    {
        const Cell &cell = m_cells.at(i);
        if (cell.canvas && cell.canvas->seriesCount() > 0)
            keepIndices.append(i);
    }

    // 2) 容量不够就自动加行（列数固定）：8 个有曲线的格按 3×2 → 行 = max(3, ceil(8/2)) = 4。
    const int requiredRows = (keepIndices.size() + cols - 1) / cols;
    const int usedRows = std::max(rows, std::max(1, requiredRows));
    const int capacity = usedRows * cols;

    QVector<Cell> next;
    next.reserve(capacity);
    for (int i : keepIndices)
        next.append(m_cells.at(i));

    // 3) 被剔掉的格连控件一起销毁；不足容量的补空画布（空白格由用户随后填曲线）。
    for (int i = 0; i < m_cells.size(); ++i)
    {
        if (keepIndices.contains(i))
            continue;
        Cell &dropped = m_cells[i];
        if (dropped.dockWidget)
        {
            if (Page *page = m_pages.at(m_activePage); page != nullptr && page->dock)
                page->dock->removeDockWidget(dropped.dockWidget);
            if (QWidget *taken = dropped.dockWidget->takeCellWidget())
                taken->setParent(nullptr);
            dropped.dockWidget->deleteLater();
            dropped.dockWidget = nullptr;
        }
        if (dropped.frame)
        {
            dropped.frame->setParent(nullptr);
            dropped.frame->deleteLater();
        }
    }
    while (next.size() < capacity)
        next.append(makeCell(nullptr));

    m_cells = next;
    m_rows = usedRows;
    m_cols = cols;
    m_active = qBound(0, m_active, m_cells.size() - 1);
    rebuildPageLayout();
    syncPage();
    return usedRows;
}

void PlotWorkspace::syncPage()
{
    if (m_pages.isEmpty())
        return;
    Page &page = *m_pages[m_activePage];
    page.cells = m_cells;
    page.rows = m_rows;
    page.cols = m_cols;
}

int PlotWorkspace::addPage(const QString &title)
{
    notifyHistoryMutation();
    auto *page = new Page;
    page->title = title.isEmpty() ? QStringLiteral("页 %1").arg(m_pages.size() + 1) : title;

    // 页容器：不再套滚动区——停靠面板要能被拖出/拖回、浮动窗也要对得准，
    // 塞在滚动区里拖动会错位（PJ4 的画面区同样不滚动）。格子多时由分隔器压缩，
    // 想看单格用最大化。
    page->host = new QWidget(this);
    page->host->setObjectName(QStringLiteral("plotPage"));
    auto *lay = new QVBoxLayout(page->host);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(3);
    m_pages.append(page);

    // 页签栏保证新页插在「＋」之前，tab 索引 0..n-1 与 m_pages 一一对应。
    return m_tabs->insertPage(page->host, page->title);
}

void PlotWorkspace::switchPage(int index)
{
    if (index < 0)
        return;
    if (index >= m_pages.size() || index == m_activePage)
        return;
    // 切页前收起当前页的全屏：隐藏的面板属于旧页，留着切回来就是一片空白。
    if (m_fullscreenCell >= 0)
    {
        for (const QPointer<DockedAxes> &dockWidget : m_fullscreenHidden)
            if (dockWidget)
                dockWidget->toggleView(true);
        m_fullscreenHidden.clear();
        m_fullscreenCell = -1;
    }
    // 旧页先把当前 cells/网格规格写回去，再载入目标页——两侧都不丢状态。
    syncPage();
    m_activePage = index;
    Page &page = *m_pages.at(index);
    m_cells = page.cells;
    m_rows = page.rows;
    m_cols = page.cols;
    m_active = m_cells.isEmpty() ? 0 : qBound(0, m_active, m_cells.size() - 1);
    m_fullscreenCell = -1;
    // 该页还没搭过停靠布局（新页/刚复制）：补一次。
    if (!m_cells.isEmpty() && !page.dock)
        rebuildPageLayout();
}

bool PlotWorkspace::removePage(int index)
{
    notifyHistoryMutation();
    if (index < 0 || index >= m_pages.size() || m_pages.size() <= 1)
        return false;

    // 关的是当前页且处于全屏：隐藏记录指向即将销毁的面板，直接清掉（不必再放回来）。
    if (index == m_activePage && m_fullscreenCell >= 0)
    {
        m_fullscreenHidden.clear();
        m_fullscreenCell = -1;
    }

    Page *page = m_pages.takeAt(index);
    // 关的是当前页：先把"当前"落到相邻页上，避免 m_cells 指向被删页的控件。
    if (index == m_activePage)
    {
        const int next = qBound(0, index, m_pages.size() - 1);
        m_activePage = next;
        m_cells = m_pages.at(next)->cells;
        m_rows = m_pages.at(next)->rows;
        m_cols = m_pages.at(next)->cols;
        m_active = 0;
        m_fullscreenCell = -1;
    }
    else if (index < m_activePage)
    {
        --m_activePage;
    }
    m_tabs->removePageAt(index);
    m_tabs->setCurrentPage(m_activePage);

    delete page->host;     // 连同页内 dock 容器与 cell 控件一起销毁
    delete page;
    return true;
}

bool PlotWorkspace::removeAt(int index)
{
    notifyHistoryMutation();
    if (index < 0 || index >= m_cells.size() || m_cells.size() <= 1)
        return false;
    Cell cell = m_cells.takeAt(index);
    // 先把它从停靠树里摘掉（面板壳随删除一起销毁），再销毁内容。
    if (cell.dockWidget)
    {
        if (!m_pages.isEmpty())
            if (Page *page = m_pages.at(m_activePage); page != nullptr && page->dock)
                page->dock->removeDockWidget(cell.dockWidget);
        if (QWidget *taken = cell.dockWidget->takeCellWidget())
            taken->setParent(nullptr);
        cell.dockWidget->deleteLater();
        cell.dockWidget = nullptr;
    }
    if (cell.frame)
    {
        cell.frame->setParent(nullptr);
        cell.frame->deleteLater();
    }
    if (m_active >= m_cells.size())
        m_active = m_cells.size() - 1;
    // 删一个后重排为单行（列数 = 数量），保持整齐（对齐老 removeCanvas → reflow）。
    m_rows = 1;
    m_cols = m_cells.size();
    rebuildPageLayout();
    syncPage();
    return true;
}

bool PlotWorkspace::createXyCanvasFor(dataset::DatasetId dsY, const QString &tY,
                                      const QString &cY)
{
    // X 通道 = 活动画布首条曲线（PJ4 语义：拖两通道，第一条已在本画布上）。
    AxesView *src = activeCanvas();
    if (!src || src->series().isEmpty())
        return false;
    const PlotSeries &x = src->series().first();
    const dataset::Dataset *dx = m_appCtx.data().get(x.datasetId);
    const dataset::Dataset *dy = m_appCtx.data().get(dsY);
    if (!dx || !dy || !dx->table(x.table) || !dy->table(tY))
        return false;

    // 整列读值 + 读时间轴（走 2.2.1 随机访问/整列 FFI，不经 LOD 意图）。
    QVector<double> xVals, yVals, xTime, yTime;
    if (dx->readColumnF64(x.table, x.column, xVals) <= 0)
        return false;
    if (dy->readColumnF64(tY, cY, yVals) <= 0)
        return false;
    if (dx->readTimeRows(x.table, 0, xVals.size(), xTime) <= 0
        || dy->readTimeRows(tY, 0, yVals.size(), yTime) <= 0)
        return false;

    // 时间最近邻对齐：以 X 时间轴为主遍历，Y 取最近样本（双指针，两轴均升序）。
    // 上限 4096 点等步抽稀——XY 图是形状预览，百万点全画没有信息增益。
    constexpr int kMaxPoints = 4096;
    const int xStep = std::max(1, int(xVals.size()) / kMaxPoints);
    QVector<nextpilot::plotscope::dataset::LodTriple> xy;
    xy.reserve(std::min<int>(xVals.size(), kMaxPoints));
    size_t j = 0;
    for (int i = 0; i < xVals.size(); i += xStep)
    {
        while (j + 1 < yTime.size()
               && std::fabs(double(yTime.at(j + 1)) - xTime.at(i))
                      <= std::fabs(double(yTime.at(j)) - xTime.at(i)))
            ++j;
        if (yVals.at(int(j)) != yVals.at(int(j))) // NaN（null 单元）跳过
            continue;
        xy.append({xVals.at(i), yVals.at(int(j)), yVals.at(int(j))});
    }
    if (xy.size() < 2)
        return false;

    // 新画布：追加一格并激活，单曲线 kind=xy；series 挂 Y 通道身份（key/重绑沿用），
    // LOD 的 x 直接是 X 通道的值——pushToPlot 原样画"值-值"折线，无需特判渲染。
    const int cellIdx = addCell();
    AxesView *canvas = m_cells.at(cellIdx).canvas;
    canvas->clear();
    const QString title =
        QStringLiteral("%1 vs %2").arg(x.column, cY);
    canvas->setChannel(dsY, tY, cY, title);
    canvas->setLodData(xy, xy.first().x, xy.last().x);
    canvas->setPlotKind(QStringLiteral("xy"));
    setActive(cellIdx);
    m_appCtx.messages().append(
        QStringLiteral("info"), QStringLiteral("plot.canvas"),
        QStringLiteral("XY 图：X=%1.%2（%3 点），Y=%4.%5")
            .arg(x.table, x.column)
            .arg(xy.size())
            .arg(tY, cY));
    return true;
}

bool PlotWorkspace::split(bool horizontal)
{
    notifyHistoryMutation();
    if (m_cells.isEmpty())
        return false;
    const int anchorIndex = qBound(0, m_active, m_cells.size() - 1);
    DockedAxes *anchor = m_cells.at(anchorIndex).dockWidget;
    Page *page = m_pages.isEmpty() ? nullptr : m_pages.at(m_activePage);
    if (!anchor || !page || !page->dock)
        return false;

    // 真正的"格内再拆"（对齐 PJ4 的 splitHorizontal/splitVertical）：新面板停靠到
    // 活动格的右/下方，只切这一块，其它格尺寸不动。
    Cell cell = makeCell(nullptr);
    const int insertAt = anchorIndex + 1;
    m_cells.insert(insertAt, cell);
    const QString title = QStringLiteral("图 %1").arg(insertAt + 1);
    cell.bar->setTitle(title);
    CellSlot slot;
    slot.title = title;
    slot.frame = cell.frame;
    slot.bar = cell.bar;
    DockedAxes *created = page->dock->addCellDock(slot, horizontal ? 1 : 2, anchor);
    if (!created)
    {
        // 停靠失败：回滚，不留半截状态。
        Cell failed = m_cells.takeAt(insertAt);
        if (failed.frame)
            failed.frame->deleteLater();
        return false;
    }
    m_cells[insertAt].dockWidget = created;
    m_active = insertAt;
    // 格内拆分出来的两块等分，别让新画布按 sizeHint 被挤成一条。
    page->dock->equalizeSplitters();
    // 拆过之后不再是规整网格：网格规格交给「m×n」重排时重算。
    syncPage();
    return true;
}

bool PlotWorkspace::duplicateActive()
{
    notifyHistoryMutation();
    AxesView *src = activeCanvas();
    if (!src)
        return false;

    // 复制 cell 骨架（与 addCell 同一装配），再逐条复制曲线 + 视图状态。
    Cell cell = makeCell(nullptr);
    // 复制曲线：PlotSeries 自含数据源标识 + LOD + 全部样式，逐条 setSeries 即完整还原。
    for (const PlotSeries &s : src->series())
        cell.canvas->setSeries(s);
    // 复制视图状态（范围/轴/图例/变换配方）。
    cell.canvas->restoreViewState(src->saveViewState());

    const int insertAt = qBound(0, m_active + 1, m_cells.size());
    m_cells.insert(insertAt, cell);
    m_rows = 1;
    m_cols = m_cells.size();
    m_active = insertAt;
    rebuildPageLayout();
    syncPage();
    return true;
}

bool PlotWorkspace::duplicatePage()
{
    notifyHistoryMutation();
    if (m_pages.isEmpty())
        return false;
    const QVector<Cell> srcCells = m_cells;
    const int srcRows = m_rows;
    const int srcCols = m_cols;

    // 新页先建好并切过去，再把源页的每一格按原顺序复制进来（曲线与视图状态一并带走）。
    const int pageIndex = addPage(QStringLiteral("页 %1 副本").arg(m_activePage + 1));
    m_tabs->setCurrentPage(pageIndex);
    if (m_activePage != pageIndex)
    {
        // currentChanged 没触发（例如同索引）：显式切一次。
        switchPage(pageIndex);
    }

    m_cells.clear();
    for (const Cell &src : srcCells)
    {
        Cell cell = makeCell(nullptr);
        if (src.canvas)
        {
            for (const PlotSeries &s : src.canvas->series())
                cell.canvas->setSeries(s);
            cell.canvas->restoreViewState(src.canvas->saveViewState());
        }
        m_cells.append(cell);
    }
    m_rows = srcRows;
    m_cols = srcCols;
    m_active = 0;
    rebuildPageLayout();
    syncPage();
    return true;
}

void PlotWorkspace::toggleFullscreen()
{
    // 退出：把全屏时藏起来的其它面板放回来（只放这一批，用户自己关掉的不会被重开）。
    if (m_fullscreenCell >= 0)
    {
        for (const QPointer<DockedAxes> &dockWidget : m_fullscreenHidden)
            if (dockWidget)
                dockWidget->toggleView(true);
        m_fullscreenHidden.clear();
        m_fullscreenCell = -1;
        return;
    }
    if (m_cells.isEmpty())
        return;

    const int index = qBound(0, m_active, m_cells.size() - 1);
    if (!m_cells.at(index).dockWidget)
    {
        rebuildPageLayout();   // 还没入布局（少见）：先搭好再全屏
        if (!m_cells.at(index).dockWidget)
            return;
    }
    // 只留这一格：ADS 的 toggleView(false) 会把空出来的分支折叠掉，
    // 幸存的那格自动填满整页（直接 setVisible(false) 做不到这一点）。
    for (int i = 0; i < m_cells.size(); ++i)
    {
        if (i == index)
            continue;
        DockedAxes *other = m_cells.at(i).dockWidget;
        if (!other)
            continue;
        other->toggleView(false);
        m_fullscreenHidden.append(other);
    }
    m_fullscreenCell = index;
    syncPage();
}

bool PlotWorkspace::setGrid(int rows, int cols)
{
    notifyHistoryMutation();
    if (rows < 1 || cols < 1)
        return false;
    // 一条命令解决三件事：剔掉没曲线的空白格、容量不够自动加行、按 m×n 严格等分重排。
    reflowGrid(rows, cols);
    return true;
}

bool PlotWorkspace::setCellType(int index, const QString &kind)
{
    notifyHistoryMutation();
    if (index < 0 || index >= m_cells.size())
        return false;
    Cell &cell = m_cells[index];
    if (isCurveKind(kind))
    {
        cell.stack->setCurrentWidget(cell.canvas);
        cell.canvas->setPlotKind(kind);
        return true;
    }
    if (isInstrumentKind(kind))
    {
        cell.instrument->switchKind(toInstrumentKind(kind));
        cell.instrument->renderFromActive(toInstrumentKind(kind));
        cell.stack->setCurrentWidget(cell.instrument);
        return true;
    }
    return false;
}

void PlotWorkspace::onPlayheadChanged(double t)
{
    for (const Cell &c : m_cells)
        if (c.canvas)
            c.canvas->setPlayhead(t, true);
}

void PlotWorkspace::syncPlaybackRange(int datasetId)
{
    if (!m_playback || datasetId < 0)
        return;
    const Dataset *ds = m_appCtx.data().get(datasetId);
    if (!ds)
        return;
    const double lo = ds->timeMin();
    const double hi = ds->timeMax();
    if (!(hi > lo))
        return;
    // 回放区间 = 数据首末时间；重置游标到起点，显示底部时间条。
    m_playback->setRange(lo, hi);
}

void PlotWorkspace::onActiveChannelChanged(int datasetId, const QString &t, const QString &c)
{
    if (m_cells.isEmpty())
        return;
    Cell &cell = m_cells[qBound(0, m_active, m_cells.size() - 1)];
    // 若活动 cell 当前显示的是仪表，把数据也喂给它；曲线画布则走 addChannel 叠加。
    if (cell.stack->currentWidget() == cell.instrument && !cell.instrument->currentKind().isEmpty())
        cell.instrument->renderFromActive(cell.instrument->currentKind());
    else
        cell.view->addChannel(datasetId, t, c, nullptr);
}

void PlotWorkspace::connectCanvasSignals(AxesView *canvas)
{
    if (!canvas)
        return;
    // 源级时间 offset provider：画布把 datasetId 经
    // SourceHub 反查源 id，再取 AppContext 的源 offset。this 长生命周期，安全。
    canvas->setSourceOffsetProvider([this](nextpilot::plotscope::dataset::DatasetId id) {
        if (id == nextpilot::plotscope::dataset::kInvalidDataset)
            return 0.0;
        const int src = m_appCtx.sources().sourceOfDataset(id);
        return src >= 0 ? m_appCtx.sourceTimeOffset(src) : 0.0;
    });
    // 同页联动游标：任一画布时间变化 → 其它画布画联动竖线（只同步时间，不复制读数）。
    // source 指针避免回环；syncLinkedTime 静默不回广播。
    QObject::connect(canvas, &AxesView::linkedTimeChanged, this,
                     [this](double t, bool visible, AxesView *source) {
                         for (const Cell &c : m_cells)
                             if (c.canvas && c.canvas != source)
                                 c.canvas->syncLinkedTime(t, visible);
                     });
    QObject::connect(canvas, &AxesView::seriesSelectionRequested, this, [this, canvas](const QString &key) {
        const PlotSeries *s = canvas->seriesByKey(key);
        if (!s)
            return;
        Selection sel;
        sel.kind = SelectionKind::Series;
        sel.label = s->legendName();
        sel.ids.insert(QStringLiteral("seriesKey"), key);
        sel.ids.insert(QStringLiteral("dataset"), s->datasetId);
        sel.ids.insert(QStringLiteral("table"), s->table);
        sel.ids.insert(QStringLiteral("column"), s->column);
        m_appCtx.setSelection(sel);
    });
    QObject::connect(canvas, &AxesView::axisSelectionRequested, this, [this](const QString &axis) {
        Selection sel;
        sel.kind = SelectionKind::Axis;
        sel.label = axis == QStringLiteral("x") ? QStringLiteral("X 轴")
                  : axis == QStringLiteral("y-left") ? QStringLiteral("左 Y 轴")
                                                      : QStringLiteral("右 Y 轴");
        sel.ids.insert(QStringLiteral("axis"), axis);
        m_appCtx.setSelection(sel);
    });
    QObject::connect(canvas, &AxesView::canvasSelectionRequested, this, [this] {
        Selection sel;
        sel.kind = SelectionKind::Canvas;
        sel.label = QStringLiteral("画布");
        sel.ids.insert(QStringLiteral("canvas"), QStringLiteral("active"));
        m_appCtx.setSelection(sel);
    });
    // 右键"拆分 / 删除此画布"：按指针反查所在格再执行。
    // 先 setActive 是必须的——右键的画布很可能不是当前激活格，
    // 直接调 split()/removeAt(m_active) 会作用到别的画布上。
    QObject::connect(canvas, &AxesView::canvasActionRequested, this,
                     [this, canvas](const QString &op) {
                         int idx = -1;
                         for (int i = 0; i < m_cells.size(); ++i)
                         {
                             if (m_cells.at(i).canvas == canvas)
                             {
                                 idx = i;
                                 break;
                             }
                         }
                         if (idx < 0)
                             return;
                         setActive(idx);
                         if (op == QLatin1String("splitHorizontal"))
                             split(true);
                         else if (op == QLatin1String("splitVertical"))
                             split(false);
                         else if (op == QLatin1String("removeCanvas"))
                         {
                             // removeAt 在"只剩一格"时拒绝：给一次就地提示，
                             // 否则用户会以为菜单项坏了（无任何反馈）。
                             if (!removeAt(idx))
                                 QToolTip::showText(QCursor::pos(), QStringLiteral("至少保留一个画布"), this);
                         }
                     });
    // 曲线 Y 变换：打开统一编辑器（按 CapAxisY 过滤 + 在线调试），回写配方。
    QObject::connect(canvas, &AxesView::seriesTransformRequested, this, [this, canvas](const QString &key) {
        const PlotSeries *s = canvas->seriesByKey(key);
        QJsonObject initial;
        if (s && !s->yTransformId.isEmpty())
        {
            TransformRequest req;
            req.descriptorId = s->yTransformId;
            req.kind = m_appCtx.transforms().get(s->yTransformId).kind;
            req.params = QJsonDocument::fromJson(s->yTransformParamsJson.toUtf8()).object();
            initial = req.toJson();
        }
        const QJsonObject req = TransformEditorDialog::run(
            this, m_appCtx, CapAxisY,
            s ? QStringLiteral("曲线 Y 变换 · %1").arg(s->legendName()) : QStringLiteral("曲线 Y 变换"),
            initial);
        if (req.isEmpty())
            return;
        canvas->setSeriesYTransform(key,
            req.value(QStringLiteral("descriptorId")).toString(),
            QString::fromUtf8(QJsonDocument(req.value(QStringLiteral("params")).toObject()).toJson(QJsonDocument::Compact)));
    });
}

// 曲线/轴/画布的属性 provider + setter。全部纯数据操作，不含业务类型分发。
// 属性 provider 注册见 commands/PropertyProviders.cpp。


void PlotWorkspace::registerViewActions()
{
    // 各域动作分文件注册（commands/），这里保留唯一注册点；路由仍按 active cell。
    registerPageActions();
    registerCanvasActions();
    registerSeriesActions();
    registerMarkupActions();
    registerPlaybackActions();

    // 全工作区快照 undo（第3仗 3b）：与画布局部视口撤销 plot.view.undo 分栈。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.workspace.undo"),
        [this](const QVariantMap &) -> Result<QVariant> {
            return QVariant(undoHistory());
        });

    // 参考线（第3仗 3c / F-2）：脚本/快捷键与右键菜单同一语义；存相对时间轴值。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.reference.set"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            AxesView *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            const double t = params.value(QStringLiteral("t")).toDouble();
            c->setReferenceTime(t);
            return QVariant(true);
        });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.reference.clear"),
        [this](const QVariantMap &) -> Result<QVariant> {
            AxesView *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            c->clearReference();
            return QVariant(true);
        });
}

// ---- 文档作用域双向通道 ----

plot::PlotDocumentSpec PlotWorkspace::toDocumentSpec() const
{
    plot::PlotDocumentSpec spec;
    spec.version = 1;
    spec.name = QStringLiteral("plot");

    // 单格导出：视图类型取"当前可见的那一支"，曲线按 origin 引用数据集，
    // 样式与视图态交给视图自己的存取接口，这里不重新解释绘图语义。
    auto canvasOf = [this](const Cell &cell) {
        plot::CanvasSpec c;
        if (!cell.canvas)
            return c;
        c.id = QString::number(cell.canvas->canvasId());
        const bool instrumentShown = cell.stack != nullptr && cell.instrument != nullptr
                                     && cell.stack->currentWidget() == cell.instrument;
        c.viewType = instrumentShown ? cell.instrument->currentKind() : cell.canvas->plotKind();
        c.title = cell.canvas->channelTitle();

        const QVector<PlotSeries> &series = cell.canvas->series();
        for (const PlotSeries &s : series)
            c.channels.append(s.legendName());
        if (!series.isEmpty())
        {
            const int sourceId = m_appCtx.sources().sourceOfDataset(series.first().datasetId);
            if (const DataSourceInfo *info = m_appCtx.sources().get(sourceId))
                c.datasetOrigin = info->origin;
        }

        QJsonArray styles;
        for (const PlotSeries &s : series)
        {
            QJsonObject o;
            o.insert(QStringLiteral("key"), s.key());
            o.insert(QStringLiteral("color"), s.color.name());
            o.insert(QStringLiteral("width"), s.lineWidth);
            o.insert(QStringLiteral("visible"), s.visible);
            o.insert(QStringLiteral("rightAxis"), s.rightAxis);
            styles.append(o);
        }
        if (!styles.isEmpty())
        {
            QJsonObject style;
            style.insert(QStringLiteral("series"), styles);
            c.style = style;
        }
        c.state = cell.canvas->saveViewState();
        return c;
    };

    for (int pi = 0; pi < m_pages.size(); ++pi)
    {
        const Page *page = m_pages.at(pi);
        plot::PlotPageSpec ps;
        ps.id = QStringLiteral("page-%1").arg(pi + 1);
        ps.title = page->title;

        // 当前页的格在 m_cells（与 page->cells 交换），其余页直接读页内副本。
        const QVector<Cell> &cells = (pi == m_activePage) ? m_cells : page->cells;
        const int rows = (pi == m_activePage) ? m_rows : page->rows;
        const int cols = (pi == m_activePage) ? m_cols : page->cols;

        QJsonObject pageState;
        pageState.insert(QStringLiteral("rows"), rows);
        pageState.insert(QStringLiteral("cols"), cols);
        pageState.insert(QStringLiteral("gridMode"), page->gridMode);
        pageState.insert(QStringLiteral("active"), pi == m_activePage ? m_active : 0);
        ps.pageState = pageState;

        // v1 只表达 m×n 等分：行 → 列 → 画布。手拆比例与 ADS 浮动/合并不在这里，
        // 那是 dock 布局的持久化范围（两通道不混，避免同一件事两处写）。
        plot::LayoutNode root;
        root.direction = QStringLiteral("vertical");
        for (int r = 0; r < rows; ++r)
        {
            plot::LayoutNode row;
            row.direction = QStringLiteral("horizontal");
            for (int c = 0; c < cols; ++c)
            {
                const int idx = r * cols + c;
                plot::LayoutNode node;
                node.isCanvas = true;
                if (idx < cells.size())
                    node.canvas = canvasOf(cells.at(idx));
                row.children.append(node);
            }
            root.children.append(row);
        }
        ps.root = root;
        spec.pages.append(ps);
    }
    return spec;
}

bool PlotWorkspace::applyDocumentSpec(const plot::PlotDocumentSpec &spec,
                                      const dataset::SourceHub &sources)
{
    if (spec.pages.isEmpty())
        return false;

    // 全局 undo 恢复期守卫：恢复过程触发的 store/结构信号不打进动作流（第3仗 3b）。
    if (m_history)
        m_history->setRestoring(true);

    // 新文档接管 pending 队列：旧文档挂起的绑定意图随旧文档作废。
    dropPendingBinds();

    // preflight 预检（2.8）：绑定前报告 origin/channel 可满足性。问题写诊断，
    // 不阻断——origin 缺失走 pending 晚到绑定，channel 缺失留空态。
    const QStringList issues = preflightDocumentSpec(spec, sources, m_appCtx.data());
    for (const QString &issue : issues)
        m_appCtx.messages().append(QStringLiteral("warn"), QStringLiteral("plot.document"), issue);

    // 树序（行优先）即格子顺序，与 toDocumentSpec 的写出口径一致。
    auto canvasesOf = [](const plot::LayoutNode &root) {
        QVector<plot::CanvasSpec> out;
        std::function<void(const plot::LayoutNode &)> walk = [&](const plot::LayoutNode &n) {
            if (n.isCanvas)
                out.append(n.canvas);
            for (const plot::LayoutNode &child : n.children)
                walk(child);
        };
        walk(root);
        return out;
    };

    // 只保留第一页作底，其余页关掉；后续页面按 spec 顺序重建。
    while (m_pages.size() > 1)
        removePage(m_pages.size() - 1);
    switchPage(0);

    for (int pi = 0; pi < spec.pages.size(); ++pi)
    {
        const plot::PlotPageSpec &ps = spec.pages.at(pi);
        if (pi > 0)
        {
            addPage(ps.title);
            m_tabs->setCurrentPage(pi);
            switchPage(pi);
        }

        const int rows = std::max(1, ps.pageState.value(QStringLiteral("rows")).toInt(1));
        const int cols = std::max(1, ps.pageState.value(QStringLiteral("cols")).toInt(1));

        // 清空当前页再按 rows×cols 补格：直接 makeCell 追加，不走 reflowGrid——
        // 后者会剔掉"没有曲线的格"，而恢复时曲线还没加进来。
        while (m_cells.size() > 1)
            removeAt(m_cells.size() - 1);
        const int need = rows * cols;
        while (m_cells.size() < need)
            m_cells.append(makeCell(nullptr));
        m_rows = rows;
        m_cols = cols;
        m_active = 0;

        const QVector<plot::CanvasSpec> canvases = canvasesOf(ps.root);
        for (int i = 0; i < m_cells.size(); ++i)
        {
            Cell &cell = m_cells[i];
            if (i >= canvases.size() || !cell.canvas)
                continue;
            const plot::CanvasSpec &c = canvases.at(i);

            const QString kind = c.viewType.isEmpty() ? QStringLiteral("timeseries") : c.viewType;
            setCellType(i, kind);

            // 读回存盘颜色：c.style.series[] 里每条的 key 形如 "datasetId|table|column"，
            // 末两段即 table.column（与 CanvasSpec.channels 的 legendName 同口径）。
            // datasetId 是进程内 id、恢复后可能漂移，故按 table.column 匹配，不按整 key。
            QHash<QString, QColor> savedColorByChannel;
            QHash<QString, bool> savedVisibleByChannel;
            const QJsonArray styleSeries = c.style.value(QStringLiteral("series")).toArray();
            for (const QJsonValue &sv : styleSeries)
            {
                const QJsonObject so = sv.toObject();
                const QStringList parts = so.value(QStringLiteral("key")).toString().split(QLatin1Char('|'));
                if (parts.size() >= 3)
                {
                    const QString ch = QStringLiteral("%1.%2").arg(parts.at(1), parts.at(2));
                    const QColor col(so.value(QStringLiteral("color")).toString());
                    if (col.isValid())
                        savedColorByChannel.insert(ch, col);
                    // 显隐随样式存盘（第3仗 3b）：undo/恢复链按通道回放。
                    savedVisibleByChannel.insert(ch, so.value(QStringLiteral("visible")).toBool(true));
                }
            }

            // 数据集回绑：origin → 源 → 该源首个数据集。匹配不上留空态并记一条，不阻断。
            const int sourceId = sources.sourceByOrigin(c.datasetOrigin);
            const DataSourceInfo *info = sourceId >= 0 ? sources.get(sourceId) : nullptr;
            if (info != nullptr && !info->datasetIds.isEmpty())
            {
                // planDatasetChoice：同一 origin 带多个数据集（扇出解析/重复加载）
                // 时按通道可满足性选最佳拟合，不盲取 first。
                const int datasetId = chooseDatasetFor(*info, c.channels);
                // 先绑全部曲线（显式色读回），再恢复视口——否则 addChannel→pushToPlot
                // 的 setXRange(m_t0,m_t1) 会用数据全范围覆盖刚恢复的视口。
                for (const QString &channel : c.channels)
                {
                    const int dot = channel.indexOf(QLatin1Char('.'));
                    const QString table = dot > 0 ? channel.left(dot) : QString();
                    const QString column = dot > 0 ? channel.mid(dot + 1) : channel;
                    if (cell.view->addChannel(datasetId, table, column, nullptr, QString(),
                                              savedColorByChannel.value(channel)))
                    {
                        // 显隐回放（第3仗 3b）：addChannel 默认可见，存盘为隐藏的补一刀。
                        if (!savedVisibleByChannel.value(channel, true))
                            cell.canvas->setSeriesVisible(
                                QStringLiteral("%1|%2|%3").arg(datasetId).arg(table, column), false);
                    }
                }
            }
            else if (!c.datasetOrigin.isEmpty())
            {
                // 源未到：挂 pending 等晚到（对标 PJ4 PendingDisplayBinder）。
                // demand reference：该格对"这个 origin 必须有源"持有需求引用，
                // 源被移除后需求仍在，同 origin 的源再次登记即自动接回。
                PendingBind p;
                p.page = pi;
                p.cell = i;
                p.origin = c.datasetOrigin;
                p.channels = c.channels;
                p.colorByChannel = savedColorByChannel;
                p.visibleByChannel = savedVisibleByChannel;
                p.state = c.state;
                p.title = c.title.isEmpty() ? c.id : c.title;
                m_pendingBinds.append(p);
                m_appCtx.sources().demandOrigin(c.datasetOrigin);
                m_appCtx.messages().append(
                    QStringLiteral("info"), QStringLiteral("plot.document"),
                    QStringLiteral("画布 %1 的数据源未到：%2，已挂起等待晚到绑定")
                        .arg(p.title, c.datasetOrigin));
            }

            // 视口属于视图、与数据无关：无论曲线是否绑成功都要恢复。restoreViewState
            // 会置位 preserveRange 标志，紧随其后的重建不再用数据范围覆盖视口。
            if (!c.state.isEmpty())
                cell.canvas->restoreViewState(c.state);
        }

        rebuildPageLayout();
        syncPage();
    }
    // 恢复链结束：动作流恢复监听，undo 基线对齐恢复后的状态。
    if (m_history)
    {
        m_history->setRestoring(false);
        m_history->noteRestored();
    }
    return true;
}

namespace {
// planDatasetChoice 的拟合打分：channels 里有多少 table.column 在该数据集
// 的 schema 里存在。ds 为空返回 -1（区分"没有数据集"和"0 条命中"）。
int scoreDatasetFit(const dataset::Dataset *ds, const QStringList &channels)
{
    if (ds == nullptr)
        return -1;
    int score = 0;
    for (const QString &channel : channels)
    {
        const int dot = channel.indexOf(QLatin1Char('.'));
        const QString table = dot > 0 ? channel.left(dot) : QString();
        const QString column = dot > 0 ? channel.mid(dot + 1) : channel;
        const dataset::Table *t = ds->table(table);
        if (t && t->column(column))
            ++score;
    }
    return score;
}
} // namespace

QStringList PlotWorkspace::preflightDocumentSpec(const plot::PlotDocumentSpec &spec,
                                                 const dataset::SourceHub &sources,
                                                 const dataset::DatasetHub &datasets)
{
    QStringList issues;

    auto canvasesOf = [](const plot::LayoutNode &root) {
        QVector<plot::CanvasSpec> out;
        std::function<void(const plot::LayoutNode &)> walk = [&](const plot::LayoutNode &n) {
            if (n.isCanvas)
                out.append(n.canvas);
            for (const plot::LayoutNode &child : n.children)
                walk(child);
        };
        walk(root);
        return out;
    };

    for (const plot::PlotPageSpec &ps : spec.pages)
    {
        const QVector<plot::CanvasSpec> canvases = canvasesOf(ps.root);
        for (const plot::CanvasSpec &c : canvases)
        {
            if (c.datasetOrigin.isEmpty())
                continue; // 空态画布是合法设计（origin 手动重绑），不报问题
            const QString name = c.title.isEmpty() ? c.id : c.title;

            const int sourceId = sources.sourceByOrigin(c.datasetOrigin);
            const DataSourceInfo *info = sourceId >= 0 ? sources.get(sourceId) : nullptr;
            if (info == nullptr)
            {
                // 不算失败：留空态 + 挂 pending，源到了自动接回；但必须报告。
                issues << QStringLiteral("画布 %1 的数据源未到：%2（已挂 pending，源登记后自动绑定）")
                                              .arg(name, c.datasetOrigin);
                continue;
            }
            if (info->datasetIds.isEmpty())
            {
                issues << QStringLiteral("画布 %1 的源 %2 尚无数据集（live 流可能还没吐数据）")
                                      .arg(name, c.datasetOrigin);
                continue;
            }

            // channel 可满足性：table/column 逐一比对数据集 schema。
            // datasetId 会漂移，取通道拟合最佳的数据集做校验（与绑定口径一致）。
            int bestDs = info->datasetIds.first();
            int bestScore = scoreDatasetFit(datasets.get(bestDs), c.channels);
            for (int k = 1; k < info->datasetIds.size(); ++k)
            {
                const int score = scoreDatasetFit(datasets.get(info->datasetIds.at(k)), c.channels);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestDs = info->datasetIds.at(k);
                }
            }
            const dataset::Dataset *ds = datasets.get(bestDs);
            if (ds == nullptr)
                continue;
            for (const QString &channel : c.channels)
            {
                const int dot = channel.indexOf(QLatin1Char('.'));
                const QString table = dot > 0 ? channel.left(dot) : QString();
                const QString column = dot > 0 ? channel.mid(dot + 1) : channel;
                const dataset::Table *t = ds->table(table);
                if (t == nullptr)
                {
                    issues << QStringLiteral("画布 %1 的表 %2 在数据集里不存在（协议/表头可能变了）")
                                          .arg(name, table);
                    continue;
                }
                if (t->column(column) == nullptr)
                    issues << QStringLiteral("画布 %1 的列 %2.%3 在数据集里不存在")
                                          .arg(name, table, column);
            }
        }
    }
    return issues;
}

int PlotWorkspace::pendingBindCount() const
{
    return m_pendingBinds.size();
}

QStringList PlotWorkspace::pendingBindOrigins() const
{
    QStringList out;
    for (const PendingBind &p : m_pendingBinds)
        out << p.origin;
    return out;
}

void PlotWorkspace::dropPendingBinds()
{
    for (const PendingBind &p : m_pendingBinds)
        m_appCtx.sources().releaseOrigin(p.origin);
    m_pendingBinds.clear();
}

void PlotWorkspace::tryBindPending()
{
    if (m_pendingBinds.isEmpty())
        return;

    // 逆序遍历便于摘除已接回的条目。
    for (int k = m_pendingBinds.size() - 1; k >= 0; --k)
    {
        PendingBind &p = m_pendingBinds[k];

        // 页/格位置仍然有效才接：页被关掉、格被重排销毁就丢弃并释放 demand
        //（布局变化属于用户新意图，晚到数据不该强行复活已消失的画布）。
        if (p.page < 0 || p.page >= m_pages.size())
        {
            m_appCtx.sources().releaseOrigin(p.origin);
            m_pendingBinds.removeAt(k);
            continue;
        }
        // 挂起条目记录的是页内格序；校验时临时切回那页（不改变用户所在页的话切换代价
        // 大——改为只在当前页校验，非当前页的条目等它所在页成为活动页时再接）。
        if (p.page != m_activePage || p.cell < 0 || p.cell >= m_cells.size())
            continue;

        const int sourceId = m_appCtx.sources().sourceByOrigin(p.origin);
        const DataSourceInfo *info = sourceId >= 0 ? m_appCtx.sources().get(sourceId) : nullptr;
        if (info == nullptr || info->datasetIds.isEmpty())
            continue; // 源还没到/还没数据，继续等下一个信号

        const int datasetId = chooseDatasetFor(*info, p.channels);
        Cell &cell = m_cells[p.cell];
        if (cell.canvas == nullptr || cell.view == nullptr)
            continue;

        bool boundAny = false;
        for (const QString &channel : p.channels)
        {
            const int dot = channel.indexOf(QLatin1Char('.'));
            const QString table = dot > 0 ? channel.left(dot) : QString();
            const QString column = dot > 0 ? channel.mid(dot + 1) : channel;
            if (cell.view->addChannel(datasetId, table, column, nullptr, QString(),
                                      p.colorByChannel.value(channel)))
            {
                boundAny = true;
                // 显隐回放（第3仗 3b）：存盘为隐藏的通道接回后保持隐藏。
                if (!p.visibleByChannel.value(channel, true))
                    cell.canvas->setSeriesVisible(
                        QStringLiteral("%1|%2|%3").arg(datasetId).arg(table, column), false);
            }
        }
        if (!boundAny)
            continue; // 通道全没接上（表头变了）：条目保留，preflight 已报告原因

        // 绑定后回放存盘视口（对标 PJ4 applySavedViewportOrZoom：bind 后重放，
        // 防止 addChannel→pushToPlot 的数据全范围 setXRange 抢走视口）。
        if (!p.state.isEmpty() && cell.canvas)
            cell.canvas->restoreViewState(p.state);

        m_appCtx.messages().append(
            QStringLiteral("info"), QStringLiteral("plot.document"),
            QStringLiteral("晚到绑定：画布 %1 的数据源 %2 已接上（%3 条通道）")
                .arg(p.title, p.origin)
                .arg(boundAny ? p.channels.size() : 0));

        m_appCtx.sources().releaseOrigin(p.origin);
        m_pendingBinds.removeAt(k);
    }
}

int PlotWorkspace::chooseDatasetFor(const dataset::DataSourceInfo &info, const QStringList &channels)
{
    if (info.datasetIds.size() <= 1)
        return info.datasetIds.value(0, -1);

    // 对标 PJ4 layout_xml::planDatasetChoice："one dataset fitting best is
    // pinned, unasked"；并列且无法询问时 "takes the first loaded candidate
    // and says so"——我们取登记序第一个并写一条诊断。
    int best = info.datasetIds.first();
    int bestScore = scoreDatasetFit(m_appCtx.data().get(best), channels);
    bool tie = false;
    for (int k = 1; k < info.datasetIds.size(); ++k)
    {
        const int did = info.datasetIds.at(k);
        const int score = scoreDatasetFit(m_appCtx.data().get(did), channels);
        if (score > bestScore)
        {
            best = did;
            bestScore = score;
            tie = false;
        }
        else if (score == bestScore)
        {
            tie = true;
        }
    }
    if (tie)
        m_appCtx.messages().append(
            QStringLiteral("warn"), QStringLiteral("plot.document"),
            QStringLiteral("源 %1 有 %2 个数据集且通道拟合并列，绑定登记序第一个（datasetId=%3）")
                .arg(info.origin)
                .arg(info.datasetIds.size())
                .arg(best));
    return best;
}

void PlotWorkspace::onDatasetTopologyChanged()
{
    // 先重绑：数据集替换后画布上还有挂在旧 id 的曲线（reload 用稳定 id 不受
    // 影响，remove 后重载才会换 id）；再接 pending（布局先到的晚到绑定）。
    rebindStaleCurves();
    tryBindPending();
}

int PlotWorkspace::rebindStaleCurves()
{
    int rebound = 0;
    for (Cell &cell : m_cells)
    {
        if (cell.view == nullptr)
            continue;
        rebound += cell.view->rebindDataset(
            [this](int oldId, const QString &table, const QString &column) {
                return resolveRebindTarget(oldId, table, column);
            });
    }
    if (rebound > 0)
        m_appCtx.messages().append(
            QStringLiteral("info"), QStringLiteral("plot.document"),
            QStringLiteral("数据集重绑：%1 条曲线按 table.column 接到新数据集").arg(rebound));
    return rebound;
}

int PlotWorkspace::resolveRebindTarget(int oldId, const QString &table, const QString &column)
{
    // 三路解析（对标 PJ4 rebindCurveKeys / CatalogModel::resolveCurveKey）：
    // 1) 旧 datasetId 仍挂在某源上 → 不动；
    // 2) 旧 id 失效但 table.column 在唯一数据集 → 静默重绑；
    // 3) 多个数据集都携带 → 取第一个并报告。
    if (oldId < 0 || m_appCtx.sources().sourceOfDataset(oldId) >= 0)
        return oldId;
    QVector<int> carriers;
    for (const int sid : m_appCtx.sources().ids())
    {
        const dataset::DataSourceInfo *info = m_appCtx.sources().get(sid);
        if (info == nullptr)
            continue;
        for (const int did : info->datasetIds)
        {
            const dataset::Dataset *ds = m_appCtx.data().get(did);
            const dataset::Table *t = ds ? ds->table(table) : nullptr;
            if (t && t->column(column))
                carriers.append(did);
        }
    }
    if (carriers.isEmpty())
        return -1;
    if (carriers.size() > 1)
        m_appCtx.messages().append(
            QStringLiteral("warn"), QStringLiteral("plot.document"),
            QStringLiteral("通道 %1.%2 有 %3 个数据集可接，重绑到第一个（datasetId=%4）")
                .arg(table, column)
                .arg(carriers.size())
                .arg(carriers.first()));
    return carriers.first();
}

QWidget *createPlotWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
{
    return new PlotWorkspace(appCtx, executor, parent);
}

} // namespace nextpilot::plotscope::gui

