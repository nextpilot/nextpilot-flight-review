#include "CanvasWorkspace.h"

#include "AppContext.h"
#include "ChannelView.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "PlaybackEngine.h"
#include "TimelineWidget.h"
#include "DatasetRegistry.h"
#include "Dataset.h"
#include "SourceRegistry.h"
#include "PropertySchema.h"
#include "Result.h"
#include "Transform.h"
#include "TransformEditorDialog.h"
#include <fastplot/Axes.h>
#include <fastplot/Panner.h>
#include <fastplot/Zoomer.h>

#include <limits>

#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
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
// 曲线类图型：在 PlotCanvas 内切（都是"曲线"模型，走 LOD 折线）。
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

// 悬浮工具栏按钮的图标：资源缺失时返回空 QIcon（按钮退化为文字，不刷告警）。
QIcon cellIcon(const char *name)
{
    const QString path = QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name));
    return QFile::exists(path) ? QIcon(path) : QIcon();
}
} // namespace

CanvasWorkspace::CanvasWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_executor(executor)
{
    // 外层纵向布局：网格画布区 + 底部时间条（对齐 PJ4 的 TimelineWidget 位置）。
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 页签容器：一页 = 一套绘图布局（对齐老 PlottingTabs）。
    // 可移动/可关闭，但至少保留一页（removePage 里挡）。
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    // 页序 = 绘图页序，拖拽改序会让页索引与页对象错位；老代码也不可拖。
    m_tabs->setMovable(false);
    m_tabs->setTabsClosable(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    outer->addWidget(m_tabs, 1);

    QObject::connect(m_tabs, &QTabWidget::currentChanged, this,
                     [this](int index) { switchPage(index); });
    QObject::connect(m_tabs, &QTabWidget::tabCloseRequested, this,
                     [this](int index) {
                         if (index >= m_pages.size())   // 末尾「＋」不参与关闭
                             return;
                         removePage(index);
                     });
    // 双击页签改名（对齐老 PlottingTabs::tabBarDoubleClicked），「＋」不参与。
    QObject::connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this,
                     [this](int index) {
                         if (index < 0 || index >= m_pages.size())
                             return;
                         bool ok = false;
                         const QString name = QInputDialog::getText(
                             this, QStringLiteral("重命名页签"), QStringLiteral("页名"),
                             QLineEdit::Normal, m_tabs->tabText(index), &ok);
                         const QString trimmed = name.trimmed();
                         if (!ok || trimmed.isEmpty())
                             return;
                         m_pages.at(index)->title = trimmed;
                         m_tabs->setTabText(index, trimmed);
                     });

    // 会话级播放引擎 + 底部时间条。引擎驱动所有画布的播放头，时间条只是它的视图。
    m_playback = new PlaybackEngine(this);
    m_timeline = new TimelineWidget(m_playback, this);
    m_timeline->setVisible(false); // 有数据载入 range 后才显示
    outer->addWidget(m_timeline);

    QObject::connect(m_playback, &PlaybackEngine::currentTimeChanged, this,
                     [this](double t) { onPlayheadChanged(t); });

    // 源级时间 offset 变化（第二梯队 #204 多源时间对齐）：统一刷新所有画布。
    // offset 是宿主态配方，画布 LOO/原始数据不动，只重推 x 映射。
    QObject::connect(&m_appCtx, &AppContext::sourceTimeOffsetChanged, this,
                     [this](int, double) {
                         for (const Cell &c : m_cells)
                             if (c.canvas)
                                 c.canvas->refreshSourceOffsets();
                     });

    // 末尾「＋」页签：老 PlottingTabs 的新建页入口（点一下＝加一页，不显示关闭按钮）。
    // 建「＋」页签时屏蔽 currentChanged：count 0→1 会把 currentIndex 从 -1 变成 0，
    // 若不屏蔽，switchPage 会把它当成"点了＋"而提前建页，和下面的 addPage 打架。
    m_tabs->blockSignals(true);
    const int plusIndex = m_tabs->addTab(new QWidget(m_tabs), QStringLiteral("＋"));
    m_tabs->tabBar()->setTabToolTip(plusIndex, QStringLiteral("新建页"));
    m_tabs->tabBar()->setTabButton(plusIndex, QTabBar::RightSide, nullptr);
    m_tabs->blockSignals(false);

    // 默认一页，页内一个画布 cell（对齐老代码的"启动就有一页"）。
    // insertTab 会把 current 留在原来的「＋」上，必须显式切回第 0 页。
    addPage(QStringLiteral("页 1"));
    m_tabs->setCurrentIndex(0);
    addCell();
    m_rows = 1;
    m_cols = 1;

    // 悬浮工具栏的自动收起：鼠标停止移动一会儿就隐藏，不让按钮常驻压住曲线。
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(1600);
    QObject::connect(m_hoverTimer, &QTimer::timeout, this, [this]() { hideHoverBars(); });

    // 活动通道只追加到当前 cell（"当前画布"语义）。
    QObject::connect(&m_appCtx, &AppContext::activeChannelChanged, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         onActiveChannelChanged(datasetId, t, c);
                         syncPlaybackRange(datasetId);
                     });

    registerViewActions();
    registerPropertyProviders();
}

CanvasWorkspace::~CanvasWorkspace()
{
    // 页对象是裸指针（Page 里还持有 scroll/host 的父子关系），析构时显式释放。
    // cell 控件归 Qt 父子树管，随 scroll 一起销毁。
    qDeleteAll(m_pages);
    m_pages.clear();
}

int CanvasWorkspace::cellCount() const
{
    return m_cells.size();
}

int CanvasWorkspace::pageCount() const
{
    return m_pages.size();
}

int CanvasWorkspace::activeIndex() const
{
    return m_active;
}

PlotCanvas *CanvasWorkspace::activeCanvas() const
{
    return m_cells.isEmpty() ? nullptr : m_cells.at(qBound(0, m_active, m_cells.size() - 1)).canvas;
}

ChannelView *CanvasWorkspace::activeChannelView() const
{
    return m_cells.isEmpty() ? nullptr : m_cells.at(qBound(0, m_active, m_cells.size() - 1)).view;
}

// 装配一块画布 cell：画布 + 仪表 + 堆叠容器，并把所有信号接好。
// addCell / split / duplicateActive 共用它，避免三份重复的装配代码各自漂移。
CanvasWorkspace::Cell CanvasWorkspace::makeCell(QWidget *parentForStack)
{
    auto *stack = new QStackedWidget(parentForStack);
    auto *canvas = new PlotCanvas(stack);
    auto *view = new ChannelView(m_executor, canvas, canvas);

    auto *instrument = new InstrumentView(m_executor, stack);
    instrument->setAppContext(&m_appCtx);

    stack->addWidget(canvas);
    stack->addWidget(instrument);
    stack->setCurrentWidget(canvas);
    // 点击任意 cell 设为活动（子控件安装事件过滤器）。
    stack->installEventFilter(this);
    // 列节点拖到画布 → 画布只解码 MIME 发信号 → 这里切活动通道
    // （与数据集树双击同一路径，activity 路由回当前 cell）。
    QObject::connect(canvas, &PlotCanvas::channelDropped, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         m_appCtx.setActiveChannel(datasetId, t, c);
                     });
    // 画布选中请求 -> 统一 selection（属性面板据此刷新）。
    connectCanvasSignals(canvas);

    Cell cell;
    cell.canvas = canvas;
    cell.view = view;
    cell.instrument = instrument;
    cell.stack = stack;
    // 每格悬浮小工具栏（对齐老 PlottingCanvas 的 m_hoverBar）：
    // 鼠标进到这一格才浮出，四个按钮直接作用在"这一格"，不必先点选。
    cell.hoverBar = makeHoverBar(cell, stack);
    // 图例从上往下让出工具栏的高度（28px + 留白），按钮与图例互不遮挡。
    if (auto *ax = canvas->plotForTest())
        ax->setLegendTopInset(34.0);
    // 鼠标在画布/仪表上移动时浮出工具栏（画布内部收事件，过滤器必须装在画布上）。
    canvas->installEventFilter(this);
    instrument->installEventFilter(this);
    // fastplot 的 Picker 吃掉了鼠标事件，父控件收不到 MouseMove；
    // 走画布自己的 pointerHovered 信号（每帧悬停回调）来浮出本格工具栏。
    QObject::connect(canvas, &PlotCanvas::pointerHovered, this, [this, canvas]() {
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

int CanvasWorkspace::addCell()
{
    m_cells.append(makeCell(nullptr));
    m_active = m_cells.size() - 1;
    // 加到当前页末尾：网格模式下按列数补位（新格落在下一行首列）。
    m_cols = std::max(1, m_cols);
    m_rows = (m_cells.size() + m_cols - 1) / m_cols;
    rebuildPageLayout();
    syncPage();
    return m_active;
}

QWidget *CanvasWorkspace::makeHoverBar(Cell &cell, QWidget *parentStack)
{
    auto *bar = new QWidget(parentStack);
    bar->setObjectName(QStringLiteral("cellHoverBar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);

    // 底色跟 Axes 的标题栏同色（palette alternateBase），高度也取标题栏高度，
    // 于是这一串按钮看起来就是标题栏右端的一段，而不是浮在曲线上的外来控件。
    QColor barBg = parentStack->palette().alternateBase().color();
    barBg.setAlpha(255);
    bar->setFixedHeight(fastplot::Axes::kTitleBarHeight - 2);
    bar->setStyleSheet(QStringLiteral(
        "#cellHoverBar { background: %1; }"
        " QToolButton { border: none; padding: 0px; border-radius: 2px; background: transparent; }"
        " QToolButton:hover { background: rgba(127,127,127,0.28); }")
        .arg(barBg.name(QColor::HexArgb)));

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(2, 0, 2, 0);
    lay->setSpacing(0);

    auto makeBtn = [bar](const char *iconName, const QString &fallbackText, const QString &tip) {
        auto *btn = new QToolButton(bar);
        btn->setAutoRaise(true);
        btn->setToolTip(tip);
        btn->setIconSize(QSize(14, 14));
        btn->setFixedSize(20, fastplot::Axes::kTitleBarHeight - 4);
        btn->setCursor(Qt::PointingHandCursor);
        const QIcon icon = cellIcon(iconName);
        if (icon.isNull())
            btn->setText(fallbackText);
        else
            btn->setIcon(icon);
        return btn;
    };

    // 顺序按用户要求：左右 · 上下 · 全屏 · 书签 · AB · 删除。
    QToolButton *leftRight = makeBtn("split_h", QStringLiteral("◫"), QStringLiteral("左右拆分"));
    QToolButton *topBottom = makeBtn("split_v", QStringLiteral("◱"), QStringLiteral("上下拆分"));
    QToolButton *maxBtn = makeBtn("maximize", QStringLiteral("⤢"), QStringLiteral("最大化 / 还原"));
    QToolButton *bookmarkBtn = makeBtn("bookmark", QStringLiteral("◇"), QStringLiteral("在当前时刻加书签"));
    QToolButton *abBtn = makeBtn("ab", QStringLiteral("AB"), QStringLiteral("A/B 区间测量"));
    QToolButton *closeBtn = makeBtn("close", QStringLiteral("✕"), QStringLiteral("关闭这一格"));

    auto cellFor = [this](QWidget *b) {
        const int i = indexOfHoverBar(b);
        return (i >= 0 && i < m_cells.size()) ? &m_cells[i] : nullptr;
    };

    QObject::connect(leftRight, &QToolButton::clicked, this, [this, bar]() {
        const int i = indexOfHoverBar(bar);
        if (i < 0)
            return;
        setActive(i);
        split(true);
        showHoverBar(m_active);
    });
    QObject::connect(topBottom, &QToolButton::clicked, this, [this, bar]() {
        const int i = indexOfHoverBar(bar);
        if (i < 0)
            return;
        setActive(i);
        split(false);
        showHoverBar(m_active);
    });
    QObject::connect(maxBtn, &QToolButton::clicked, this, [this, bar]() {
        const int i = indexOfHoverBar(bar);
        if (i < 0)
            return;
        setActive(i);
        toggleFullscreen();
        showHoverBar(m_active);
    });
    // 书签：以当前游标时刻为准；没有游标就取可见范围中点（总有明确落点）。
    QObject::connect(bookmarkBtn, &QToolButton::clicked, this, [cellFor, bar]() {
        Cell *c = cellFor(bar);
        if (!c || !c->canvas)
            return;
        double t = c->canvas->cursorActiveForTest() ? c->canvas->cursorTime() : 0.0;
        if (!c->canvas->cursorActiveForTest())
        {
            double t0 = 0.0;
            double t1 = 0.0;
            if (c->canvas->visibleTimeRange(&t0, &t1))
                t = (t0 + t1) / 2.0;
        }
        c->canvas->addBookmark(t, QStringLiteral("书签"));
    });
    // A/B：与该画布右键/Ctrl+左键同一条实现（以游标为中心生成区间）。
    QObject::connect(abBtn, &QToolButton::clicked, this, [cellFor, bar]() {
        Cell *c = cellFor(bar);
        if (c && c->canvas)
            c->canvas->toggleAB();
    });
    QObject::connect(closeBtn, &QToolButton::clicked, this, [this, bar]() {
        const int i = indexOfHoverBar(bar);
        if (i >= 0)
            removeAt(i);
    });

    lay->addWidget(leftRight);
    lay->addWidget(topBottom);
    lay->addWidget(maxBtn);
    lay->addWidget(bookmarkBtn);
    lay->addWidget(abBtn);
    lay->addWidget(closeBtn);
    bar->hide();
    return bar;
}
void CanvasWorkspace::flashToolbar(bool autoHide)
{
    showHoverBar(m_active);
    if (!autoHide && m_hoverTimer)
        m_hoverTimer->stop();
}

void CanvasWorkspace::layoutHoverBar(Cell &cell)
{
    if (!cell.hoverBar || !cell.stack)
        return;
    cell.hoverBar->adjustSize();
    // 对齐 Axes 标题栏：同一条横带（y 与高度都取标题栏），右端贴绘图区右边界。
    // 坐标从 Axes 换到 stack：canvas 与 axes 同尺寸时偏移为 0，这里仍显式换算，
    // 免得将来加内边距后按钮跑到标题栏外面。
    int rightPx = cell.stack->width() - 4;
    if (cell.canvas && cell.canvas->plotForTest())
    {
        const QRectF plot = cell.canvas->plotForTest()->plotRect();
        const QPoint anchor = cell.canvas->plotForTest()->mapTo(
            cell.stack, QPoint(int(plot.right()), 1));
        rightPx = anchor.x();
    }
    const int x = qMax(0, rightPx - cell.hoverBar->width() - 2);
    cell.hoverBar->move(x, 1);
    cell.hoverBar->raise();
}

int CanvasWorkspace::indexOfHoverBar(const QObject *bar) const
{
    if (!bar)
        return -1;
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells.at(i).hoverBar == bar)
            return i;
    return -1;
}

void CanvasWorkspace::showHoverBar(int index)
{
    if (index < 0 || index >= m_cells.size())
        return;
    // 只浮出当前这一格的工具栏（多格时各自独立，不互相串）。
    for (int i = 0; i < m_cells.size(); ++i)
    {
        if (i == index)
            continue;
        if (m_cells.at(i).hoverBar)
            m_cells.at(i).hoverBar->hide();
    }
    Cell &cell = m_cells[index];
    if (!cell.hoverBar)
        return;
    layoutHoverBar(cell);
    cell.hoverBar->show();
    if (m_hoverTimer)
        m_hoverTimer->start();
}

void CanvasWorkspace::hideHoverBars()
{
    for (const Cell &c : m_cells)
        if (c.hoverBar)
            c.hoverBar->hide();
}

bool CanvasWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // 这一格内的对象（stack 本身 / 画布 / 仪表）都被装了过滤器：统一映射回 cell 索引。
    int cellIndex = -1;
    for (int i = 0; i < m_cells.size(); ++i)
    {
        const Cell &c = m_cells.at(i);
        if (watched == c.stack || watched == c.canvas || watched == c.instrument
            || (c.hoverBar && watched == c.hoverBar))
        {
            cellIndex = i;
            break;
        }
    }

    if (cellIndex >= 0)
    {
        if (type == QEvent::MouseButtonPress)
            setActive(cellIndex);
        else if (type == QEvent::Enter && watched == m_cells.at(cellIndex).stack)
            showHoverBar(cellIndex);       // 指针进到这一格 → 浮出本格工具栏
        else if (type == QEvent::Resize && watched == m_cells.at(cellIndex).stack)
            layoutHoverBar(m_cells[cellIndex]);
    }

    return QWidget::eventFilter(watched, event);
}

void CanvasWorkspace::setActive(int index)
{
    if (index < 0 || index >= m_cells.size() || m_active == index)
        return;
    m_active = index;
}

void CanvasWorkspace::detachStacks()
{
    // 重建 splitter 树前必须先把 cell 控件移出去：QSplitter 析构会删掉它持有的子控件，
    // 而 cell 的 stack 归本类所有（还要复用），不能被连带删除。
    for (const Cell &c : m_cells)
    {
        if (!c.stack)
            continue;
        c.stack->setParent(nullptr);
        c.stack->hide();
    }
}

void CanvasWorkspace::rebuildPageLayout()
{
    if (m_pages.isEmpty())
        return;
    Page &page = *m_pages[m_activePage];
    if (!page.host)
        return;

    detachStacks();
    // 拆掉旧树（此时树里已经没有 cell，安全）。
    if (page.root)
    {
        delete page.root;
        page.root = nullptr;
    }

    // 对齐老 PlottingLayout：根=竖直 splitter（一行一个子项），行=水平 splitter（一格里一块画布）。
    // 行高给下限，画布多到一屏放不下时由外层 QScrollArea 出竖直滚动条。
    constexpr int kRowMinHeight = 200;
    page.root = new QSplitter(Qt::Vertical, page.host);
    page.root->setObjectName(QStringLiteral("plotPageRoot"));
    page.root->setHandleWidth(6);
    page.root->setChildrenCollapsible(false);
    if (auto *lay = qobject_cast<QVBoxLayout *>(page.host->layout()))
        lay->addWidget(page.root);
    else
    {
        auto *lay2 = new QVBoxLayout(page.host);
        lay2->setContentsMargins(0, 0, 0, 0);
        lay2->setSpacing(0);
        lay2->addWidget(page.root);
    }

    const int cols = std::max(1, m_cols);
    const int rows = std::max(1, m_rows);
    int index = 0;
    for (int r = 0; r < rows; ++r)
    {
        auto *rowSplitter = new QSplitter(Qt::Horizontal, page.root);
        rowSplitter->setHandleWidth(6);
        rowSplitter->setChildrenCollapsible(false);
        rowSplitter->setMinimumHeight(kRowMinHeight);
        page.root->addWidget(rowSplitter);
        for (int c = 0; c < cols && index < m_cells.size(); ++c, ++index)
        {
            Cell &cell = m_cells[index];
            cell.stack->setParent(rowSplitter);
            cell.stack->show();
            rowSplitter->addWidget(cell.stack);
        }
    }
    page.host->setMinimumHeight(rows * kRowMinHeight);
    // 行内均分：QSplitter 默认按 sizeHint 分配，空白画布会被挤成 0 宽。
    QTimer::singleShot(0, page.root, [rowsSplit = page.root, rows]() {
        QList<int> sizes;
        for (int i = 0; i < rowsSplit->count(); ++i)
            sizes.append(1);
        if (!sizes.isEmpty())
            rowsSplit->setSizes(sizes);
    });
    page.gridMode = true;
}

void CanvasWorkspace::syncPage()
{
    if (m_pages.isEmpty())
        return;
    Page &page = *m_pages[m_activePage];
    page.cells = m_cells;
    page.rows = m_rows;
    page.cols = m_cols;
}

int CanvasWorkspace::addPage(const QString &title)
{
    auto *page = new Page;
    page->title = title.isEmpty() ? QStringLiteral("页 %1").arg(m_pages.size() + 1) : title;

    // 页容器：竖直滚动（对齐老 PlottingPage）——画布多到一屏放不下时往下滚，
    // 而不是把每格压成一条。
    page->scroll = new QScrollArea(this);
    page->scroll->setObjectName(QStringLiteral("plotPage"));
    page->scroll->setWidgetResizable(true);
    page->scroll->setFrameShape(QFrame::NoFrame);
    page->scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    page->host = new QWidget(page->scroll);
    auto *lay = new QVBoxLayout(page->host);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(3);
    page->scroll->setWidget(page->host);
    m_pages.append(page);

    // 永远排在末尾「＋」之前，保证 tab 索引 0..n-1 与 m_pages 一一对应。
    const int plusIndex = qMax(0, m_tabs->count() - 1);
    const int index = m_tabs->insertTab(plusIndex, page->scroll, page->title);
    m_tabs->tabBar()->setTabToolTip(index, QStringLiteral("绘图页：双击页签可重命名"));
    return index;
}

void CanvasWorkspace::switchPage(int index)
{
    if (index < 0)
        return;
    if (index >= m_pages.size())
    {
        // 点到末尾「＋」：新建一页并切过去（老 PlottingTabs::appendPlusTab 的行为）。
        const int created = addPage();
        m_tabs->setCurrentIndex(created);   // 触发本函数处理新页
        if (m_cells.isEmpty())
            addCell();                       // 新页先给一格空画布
        m_rows = 1;
        m_cols = 1;
        return;
    }
    if (index == m_activePage)
        return;
    // 旧页先把当前 cells/网格规格写回去，再载入目标页——两侧都不丢状态。
    syncPage();
    m_activePage = index;
    Page &page = *m_pages.at(index);
    m_cells = page.cells;
    m_rows = page.rows;
    m_cols = page.cols;
    m_active = m_cells.isEmpty() ? 0 : qBound(0, m_active, m_cells.size() - 1);
    m_fullscreenCell = -1;
}

bool CanvasWorkspace::removePage(int index)
{
    if (index < 0 || index >= m_pages.size() || m_pages.size() <= 1)
        return false;

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
    m_tabs->blockSignals(true);
    m_tabs->removeTab(index);
    m_tabs->blockSignals(false);
    m_tabs->setCurrentIndex(m_activePage);

    delete page->scroll;   // 连同 host 与页内 cell 控件一起销毁
    delete page;
    return true;
}

bool CanvasWorkspace::removeAt(int index)
{
    if (index < 0 || index >= m_cells.size() || m_cells.size() <= 1)
        return false;
    Cell cell = m_cells.takeAt(index);
    if (cell.stack)
    {
        cell.stack->setParent(nullptr);
        cell.stack->deleteLater();
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

bool CanvasWorkspace::split(bool horizontal)
{
    if (m_cells.isEmpty())
        return false;
    // 拆分：在当前活动格之后插一块新画布。水平拆分往右长一列，垂直拆分往下长一行；
    // 与老 PlottingLayout::split 的结果一致（同一格里对半、其它格尺寸不动），
    // 差别只是本实现统一按网格重排，避免"拆分树"和"网格模式"两套几何互相打架。
    Cell cell = makeCell(nullptr);
    const int insertAt = qBound(0, m_active + 1, m_cells.size());
    m_cells.insert(insertAt, cell);
    if (horizontal)
    {
        m_cols = m_cells.size();
        m_rows = 1;
    }
    else
    {
        m_cols = 1;
        m_rows = m_cells.size();
    }
    m_active = insertAt;
    rebuildPageLayout();
    syncPage();
    return true;
}

bool CanvasWorkspace::duplicateActive()
{
    PlotCanvas *src = activeCanvas();
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

bool CanvasWorkspace::duplicatePage()
{
    if (m_pages.isEmpty())
        return false;
    const QVector<Cell> srcCells = m_cells;
    const int srcRows = m_rows;
    const int srcCols = m_cols;

    // 新页先建好并切过去，再把源页的每一格按原顺序复制进来（曲线与视图状态一并带走）。
    const int pageIndex = addPage(QStringLiteral("页 %1 副本").arg(m_activePage + 1));
    m_tabs->setCurrentIndex(pageIndex);
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

void CanvasWorkspace::toggleFullscreen()
{
    if (m_cells.isEmpty())
        return;
    // 退出：重建布局（rebuildPageLayout 会把所有格恢复可见）。
    if (m_fullscreenCell >= 0)
    {
        m_fullscreenCell = -1;
        rebuildPageLayout();
        syncPage();
        return;
    }
    m_fullscreenCell = m_active;
    rebuildPageLayout();
    syncPage();
    // 只留活动格可见；不含可见格的行随之隐藏，避免顶部/底部留下一条空 splitter。
    for (int i = 0; i < m_cells.size(); ++i)
        m_cells.at(i).stack->setVisible(i == m_fullscreenCell);
    Page &page = *m_pages[m_activePage];
    if (!page.root)
        return;
    for (int i = 0; i < page.root->count(); ++i)
    {
        auto *row = qobject_cast<QSplitter *>(page.root->widget(i));
        if (!row)
            continue;
        bool anyVisible = false;
        for (int j = 0; j < row->count(); ++j)
            if (row->widget(j)->isVisible())
                anyVisible = true;
        row->setVisible(anyVisible);
    }
}

bool CanvasWorkspace::setGrid(int rows, int cols)
{
    if (rows < 1 || cols < 1)
        return false;
    const int want = rows * cols;
    while (m_cells.size() < want)
        m_cells.append(makeCell(nullptr));
    while (m_cells.size() > want && m_cells.size() > 1)
    {
        Cell cell = m_cells.takeLast();
        if (cell.stack)
        {
            cell.stack->setParent(nullptr);
            cell.stack->deleteLater();
        }
    }
    m_rows = rows;
    m_cols = cols;
    m_active = qBound(0, m_active, m_cells.size() - 1);
    m_fullscreenCell = -1;
    rebuildPageLayout();
    syncPage();
    return true;
}

bool CanvasWorkspace::setCellType(int index, const QString &kind)
{
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

void CanvasWorkspace::onPlayheadChanged(double t)
{
    for (const Cell &c : m_cells)
        if (c.canvas)
            c.canvas->setPlayhead(t, true);
}

void CanvasWorkspace::syncPlaybackRange(int datasetId)
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
    m_timeline->setVisible(true);
}

void CanvasWorkspace::onActiveChannelChanged(int datasetId, const QString &t, const QString &c)
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

void CanvasWorkspace::connectCanvasSignals(PlotCanvas *canvas)
{
    if (!canvas)
        return;
    // 源级时间 offset provider（#204 多源时间对齐）：画布把 datasetId 经
    // SourceRegistry 反查源 id，再取 AppContext 的源 offset。this 长生命周期，安全。
    canvas->setSourceOffsetProvider([this](nextpilot::plotscope::dataset::DatasetId id) {
        if (id == nextpilot::plotscope::dataset::kInvalidDataset)
            return 0.0;
        const int src = m_appCtx.sources().sourceOfDataset(id);
        return src >= 0 ? m_appCtx.sourceTimeOffset(src) : 0.0;
    });
    // 同页联动游标：任一画布时间变化 → 其它画布画联动竖线（只同步时间，不复制读数）。
    // source 指针避免回环；syncLinkedTime 静默不回广播。
    QObject::connect(canvas, &PlotCanvas::linkedTimeChanged, this,
                     [this](double t, bool visible, PlotCanvas *source) {
                         for (const Cell &c : m_cells)
                             if (c.canvas && c.canvas != source)
                                 c.canvas->syncLinkedTime(t, visible);
                     });
    QObject::connect(canvas, &PlotCanvas::seriesSelectionRequested, this, [this, canvas](const QString &key) {
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
    QObject::connect(canvas, &PlotCanvas::axisSelectionRequested, this, [this](const QString &axis) {
        Selection sel;
        sel.kind = SelectionKind::Axis;
        sel.label = axis == QStringLiteral("x") ? QStringLiteral("X 轴")
                  : axis == QStringLiteral("y-left") ? QStringLiteral("左 Y 轴")
                                                      : QStringLiteral("右 Y 轴");
        sel.ids.insert(QStringLiteral("axis"), axis);
        m_appCtx.setSelection(sel);
    });
    QObject::connect(canvas, &PlotCanvas::canvasSelectionRequested, this, [this] {
        Selection sel;
        sel.kind = SelectionKind::Canvas;
        sel.label = QStringLiteral("画布");
        sel.ids.insert(QStringLiteral("canvas"), QStringLiteral("active"));
        m_appCtx.setSelection(sel);
    });
    // 曲线 Y 变换：打开统一编辑器（按 CapAxisY 过滤 + 在线调试），回写配方。
    QObject::connect(canvas, &PlotCanvas::seriesTransformRequested, this, [this, canvas](const QString &key) {
        const PlotSeries *s = canvas->seriesByKey(key);
        QJsonObject initial;
        if (s && !s->yTransformId.isEmpty())
        {
            TransformRequest req;
            req.descriptorId = s->yTransformId;
            req.kind = TransformRegistry::get(s->yTransformId).kind;
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
void CanvasWorkspace::registerPropertyProviders()
{
    auto row = [](const QString &key, const QString &label, const QString &value,
                  PropKind kind = PropKind::ReadOnly, bool editable = false) {
        PropRow r;
        r.key = key;
        r.label = label;
        r.value = value;
        r.kind = kind;
        r.editable = editable;
        return r;
    };

    // --- 曲线 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Series, [this, row](const Selection &sel) {
        const QString key = sel.ids.value(QStringLiteral("seriesKey")).toString();
        PlotCanvas *canvas = activeCanvas();
        const PlotSeries *s = canvas ? canvas->seriesByKey(key) : nullptr;
        PropertySchema schema;
        schema.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("series");
        o.label = QStringLiteral("曲线");
        o.rows << row(QStringLiteral("title"), QStringLiteral("标题"), s ? s->title : sel.label);
        o.rows << row(QStringLiteral("channel"), QStringLiteral("通道"),
                      s ? s->legendName() : sel.label);
        if (s)
        {
            o.rows << row(QStringLiteral("visible"), QStringLiteral("显示"),
                          s->visible ? QStringLiteral("true") : QStringLiteral("false"),
                          PropKind::Bool, true);
            o.rows << row(QStringLiteral("color"), QStringLiteral("颜色"), s->color.name(),
                          PropKind::Color, true);
            o.rows << row(QStringLiteral("width"), QStringLiteral("线宽"),
                          QString::number(s->lineWidth, 'f', 2), PropKind::Number, true);
            o.rows << row(QStringLiteral("axis"), QStringLiteral("坐标轴"),
                          s->rightAxis ? QStringLiteral("右 Y 轴") : QStringLiteral("左 Y 轴"),
                          PropKind::Enum, true);
            o.rows[o.rows.size() - 1].options = {QStringLiteral("左 Y 轴"), QStringLiteral("右 Y 轴")};
        }
        schema.objects << o;

        // 变换组：X 轴时间 offset + Y 轴变换（算子/表达式/Lua），按能力过滤，防止选错。
        PropObject tr;
        tr.id = QStringLiteral("transform");
        tr.label = QStringLiteral("变换");
        tr.rows << row(QStringLiteral("xTransform"), QStringLiteral("X 轴时间偏移(s)"),
                       s ? QString::number(s->xTimeOffset, 'f', 3) : QStringLiteral("0.000"),
                       PropKind::Number, true);
        const QString yDesc = s && !s->yTransformId.isEmpty()
            ? TransformRegistry::get(s->yTransformId).label : QStringLiteral("无");
        tr.rows << row(QStringLiteral("yTransform"), QStringLiteral("Y 轴变换"), yDesc,
                       PropKind::Text, true);
        tr.rows.last().tooltip = QStringLiteral("可选算子/表达式/Lua；在右键菜单或脚本里用 "
                                                "plot.series.transform 设置（能力：轴Y）。可选清单见 "
                                                "TransformRegistry::idsByCapability(CapAxisY)");
        schema.objects << tr;
        return schema.toJson();
    });

    // --- 轴 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Axis, [this, row](const Selection &sel) {
        const QString axis = sel.ids.value(QStringLiteral("axis")).toString();
        PlotCanvas *canvas = activeCanvas();
        PropertySchema schema;
        schema.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("axis");
        o.label = sel.label;
        o.rows << row(QStringLiteral("id"), QStringLiteral("轴"), axis);
        if (canvas && canvas->plotWidget())
        {
            o.rows << row(QStringLiteral("grid"), QStringLiteral("网格"),
                          canvas->plotWidget()->gridVisible() ? QStringLiteral("true") : QStringLiteral("false"),
                          PropKind::Bool, true);
        }
        const QString tfId = canvas ? canvas->axisTransform(axis) : QString();
        const QString tfLabel = tfId.isEmpty()
            ? QStringLiteral("无（填 Transform id 或算子/表达式/Lua）")
            : TransformRegistry::get(tfId).label;
        if (axis == QStringLiteral("x"))
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("X 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("X 轴变换 id。可选：TransformRegistry::idsByCapability(CapAxisX)，"
                                                    "如 builtin.scale 的 time_offset_sec / expression");
        }
        else
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("Y 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("Y 轴变换 id。可选：TransformRegistry::idsByCapability(CapAxisY)");
        }
        schema.objects << o;
        return schema.toJson();
    });

    // --- 画布 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Canvas, [this, row](const Selection &sel) {
        PlotCanvas *canvas = activeCanvas();
        PropertySchema schema;
        schema.title = QStringLiteral("画布属性");
        PropObject o;
        o.id = QStringLiteral("canvas");
        o.label = QStringLiteral("画布");
        o.rows << row(QStringLiteral("kind"), QStringLiteral("图型"),
                      canvas ? canvas->plotKind() : QString());
        o.rows << row(QStringLiteral("series"), QStringLiteral("曲线数"),
                      QString::number(canvas ? canvas->seriesCount() : 0));
        o.rows << row(QStringLiteral("legend"), QStringLiteral("图例"),
                      canvas && canvas->legendVisible() ? QStringLiteral("true") : QStringLiteral("false"),
                      PropKind::Bool, true);
        schema.objects << o;
        return schema.toJson();
    });

    // --- setter：把属性写回活动画布 ---
    m_appCtx.interactions().registerPropertySetter(SelectionKind::Series,
        [this](const Selection &sel, const QString &, const QString &key, const QString &value, QString *err) {
            PlotCanvas *canvas = activeCanvas();
            if (!canvas)
            {
                if (err) *err = QStringLiteral("没有活动画布");
                return false;
            }
            const QString k = sel.ids.value(QStringLiteral("seriesKey")).toString();
            if (key == QStringLiteral("visible"))
                return canvas->setSeriesVisible(k, value == QStringLiteral("true"));
            if (key == QStringLiteral("color"))
                return canvas->setSeriesInk(k, QColor(value));
            if (key == QStringLiteral("width"))
                return canvas->setSeriesWidth(k, value.toDouble());
            if (key == QStringLiteral("axis"))
                return canvas->setSeriesAxis(k, value == QStringLiteral("右 Y 轴"));
            if (key == QStringLiteral("xTransform"))
                return canvas->setSeriesXOffset(k, value.toDouble());
            if (key == QStringLiteral("yTransform"))
                return canvas->setSeriesYTransform(k, value);
            if (err) *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });

    m_appCtx.interactions().registerPropertySetter(SelectionKind::Axis,
        [this](const Selection &sel, const QString &, const QString &key, const QString &value, QString *err) {
            PlotCanvas *canvas = activeCanvas();
            if (!canvas || !canvas->plotWidget())
            {
                if (err) *err = QStringLiteral("没有活动画布");
                return false;
            }
            if (key == QStringLiteral("grid"))
            {
                canvas->plotWidget()->grid(value == QStringLiteral("true"));
                return true;
            }
            if (key == QStringLiteral("transform"))
            {
                // 轴变换通过 TransformRegistry 的 id 生效；实际执行在曲线内（Task #123）。
                if (!value.isEmpty() && !TransformRegistry::contains(value))
                {
                    if (err) *err = QStringLiteral("未知变换 id: %1").arg(value);
                    return false;
                }
                canvas->setAxisTransform(sel.ids.value(QStringLiteral("axis")).toString(), value);
                return true;
            }
            if (err) *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });

    m_appCtx.interactions().registerPropertySetter(SelectionKind::Canvas,
        [this](const Selection &, const QString &, const QString &key, const QString &value, QString *err) {
            PlotCanvas *canvas = activeCanvas();
            if (!canvas)
            {
                if (err) *err = QStringLiteral("没有活动画布");
                return false;
            }
            if (key == QStringLiteral("legend"))
            {
                canvas->setLegendVisible(value == QStringLiteral("true"));
                return true;
            }
            if (err) *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });
}

void CanvasWorkspace::registerViewActions()
{
    // 所有 plot.* 只在这里注册一次，路由到 active cell —— 多画布不会被后建者劫持。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.export.png"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(QStringLiteral("epath"), QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportPng(path))
                                        return fail(QStringLiteral("eio"), QStringLiteral("PNG 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.export.csv"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(QStringLiteral("epath"), QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportCsv(path))
                                        return fail(QStringLiteral("eio"), QStringLiteral("CSV 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.save"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return activeCanvas() ? QVariant(activeCanvas()->saveViewState())
                                                          : QVariant(QVariantMap());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.restore"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const auto state = params.value(QStringLiteral("state")).toJsonObject();
                                    if (!activeCanvas() || !activeCanvas()->restoreViewState(state))
                                        return fail(QStringLiteral("estate"), QStringLiteral("视图状态复原失败"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!activeCanvas())
                                        return QVariant(false);
                                    activeCanvas()->setLegendVisible(!activeCanvas()->legendVisible());
                                    return QVariant(activeCanvas()->legendVisible());
                                });

    // 读数游标 / A-B / 书签（对齐老 PlottingCanvas 交互，view action 供菜单/快捷键/测试统一触发）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.cursor.pinned"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    c->setCursorTime(c->cursorTime(), !c->cursorPinned());
                                    return QVariant(c->cursorPinned());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.cursor.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (PlotCanvas *c = activeCanvas()) { c->clearCursor(); c->clearAB(); }
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.ab.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    c->toggleAB();
                                    return QVariant(c->abActive());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.bookmark.add"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    // 「标记」：在**当前页所有画布**的同一时刻各画一条竖线
                                    // （用户要求：绘制时所有 axes 同步添加），而不是只加活动那格。
                                    PlotCanvas *ref = activeCanvas();
                                    for (const Cell &cell : m_cells)
                                        if (cell.canvas && !ref)
                                            ref = cell.canvas;
                                    if (!ref)
                                        return QVariant(false);
                                    const double t = params.contains(QStringLiteral("t"))
                                                         ? params.value(QStringLiteral("t")).toDouble()
                                                         : ref->cursorTime();
                                    const QString label =
                                        params.value(QStringLiteral("label")).toString();
                                    int added = 0;
                                    for (const Cell &cell : m_cells)
                                    {
                                        if (!cell.canvas)
                                            continue;
                                        cell.canvas->addBookmark(t, label);
                                        ++added;
                                    }
                                    return QVariant(added);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.bookmark.list"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantList list;
                                    if (PlotCanvas *c = activeCanvas())
                                        for (const PlotCanvas::Bookmark &bm : c->bookmarks())
                                            list.append(QVariantMap{
                                                {QStringLiteral("id"), bm.id},
                                                {QStringLiteral("t"), bm.t},
                                                {QStringLiteral("label"), bm.label},
                                            });
                                    return QVariant(list);
                                });

    // Marker 规则产出：把 PlotMarker 列表画到活动画布（竖线/区间带/值带/label）。
    // params["markers"] 为 QVariantList<QVariantMap>（每项是 PlotMarker::toJson 的 map）。
    auto applyMarkers = [this](const QVariantMap &params) -> Result<QVariant> {
        PlotCanvas *c = activeCanvas();
        if (!c)
            return QVariant(false);
        QJsonArray arr = QJsonArray::fromVariantList(
            params.value(QStringLiteral("markers")).toList());
        c->setMarkers(arr);
        return QVariant(c->markerCount());
    };
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.markers.set"), applyMarkers);
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.markers.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (PlotCanvas *c = activeCanvas())
                                        c->setMarkers(QJsonArray());
                                    return QVariant(true);
                                });

    // 状态视图（对齐 PJ4 StateTransitionsView）：rows = [{channel, segments:[…]}]，
    // 画到活动画布顶部的状态色带区（host 的 data.state intent 负责分段取数）。
    // params["mode"]=="append" 时按 channel 名合并（多通道多行叠加）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.set"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    QJsonArray arr = QJsonArray::fromVariantList(
                                        params.value(QStringLiteral("rows")).toList());
                                    const bool append =
                                        params.value(QStringLiteral("mode")).toString() == QStringLiteral("append");
                                    const bool ok = c->setStateRows(arr, append);
                                    return QVariant(ok ? c->stateRowCount() : 0);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (PlotCanvas *c = activeCanvas())
                                        c->setStateRows(QJsonArray());
                                    return QVariant(true);
                                });

    // === 图例交互（第二梯队 #205，对齐 PJ4）===
    // 点击条目切显隐 / 折叠 / 字号缩放的脚本与菜单入口（GUI 点击直接走画布命中）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.toggleItem"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    if (!c || key.isEmpty())
                                        return QVariant(false);
                                    return QVariant(c->toggleSeriesVisible(key));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.collapse"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    const bool target = params.contains(QStringLiteral("collapsed"))
                                                            ? params.value(QStringLiteral("collapsed")).toBool()
                                                            : !c->legendCollapsed(); // 无参 = 切换
                                    c->setLegendCollapsed(target);
                                    return QVariant(target);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.fontZoom"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    const int steps = params.value(QStringLiteral("steps")).toInt();
                                    c->zoomLegendFont(steps);
                                    return QVariant(c->legendFontScale());
                                });

    // === 多源时间对齐（第二梯队 #204，对齐 PJ4 SourceTimelineController）===
    // 源级显示时间偏移（秒）：施加在"曲线 x + 源 offset"，不改数据。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.offset.set"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    bool okId = false;
                                    const int src = params.value(QStringLiteral("sourceId")).toInt(&okId);
                                    if (!okId || src < 0)
                                        return Error{QStringLiteral("eargs"),
                                                     QStringLiteral("需要 sourceId 参数")};
                                    const double off = params.value(QStringLiteral("offsetSec")).toDouble();
                                    m_appCtx.setSourceTimeOffset(src, off);
                                    return QVariant(off);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.offset.clear"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (params.contains(QStringLiteral("sourceId")))
                                    {
                                        m_appCtx.setSourceTimeOffset(
                                            params.value(QStringLiteral("sourceId")).toInt(), 0.0);
                                    }
                                    else
                                    {
                                        for (int src : m_appCtx.sourceTimeOffsetIds())
                                            m_appCtx.setSourceTimeOffset(src, 0.0);
                                    }
                                    return QVariant(true);
                                });
    // 一键对齐：mode = start|center|end。按各源时间范围平移，使所有源
    // 的开始/中点/结束落在同一时刻（基准取全局极值），返回 {sourceId: offset}。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.align"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString mode =
                                        params.value(QStringLiteral("mode")).toString();
                                    if (mode != QStringLiteral("start")
                                        && mode != QStringLiteral("center")
                                        && mode != QStringLiteral("end"))
                                        return Error{QStringLiteral("eargs"),
                                                     QStringLiteral("mode 需为 start/center/end")};
                                    // 每源聚合时间范围（数据集首尾时间）。
                                    QHash<int, QPair<double, double>> ranges; // src -> [tMin,tMax]
                                    for (const auto dsId : m_appCtx.data().ids())
                                    {
                                        const Dataset *ds = m_appCtx.data().get(dsId);
                                        if (!ds || ds->tableCount() == 0)
                                            continue;
                                        const int src = m_appCtx.sources().sourceOfDataset(dsId);
                                        if (src < 0)
                                            continue;
                                        const double lo = ds->timeMin();
                                        const double hi = ds->timeMax();
                                        if (!qIsFinite(lo) || !qIsFinite(hi))
                                            continue;
                                        auto it = ranges.find(src);
                                        if (it == ranges.end())
                                            ranges.insert(src, {lo, hi});
                                        else
                                        {
                                            it.value().first = qMin(it.value().first, lo);
                                            it.value().second = qMax(it.value().second, hi);
                                        }
                                    }
                                    if (ranges.isEmpty())
                                        return Error{QStringLiteral("enodata"),
                                                     QStringLiteral("无可用数据源")};
                                    double gLo = std::numeric_limits<double>::max();
                                    double gHi = std::numeric_limits<double>::lowest();
                                    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it)
                                    {
                                        gLo = qMin(gLo, it.value().first);
                                        gHi = qMax(gHi, it.value().second);
                                    }
                                    QVariantMap applied;
                                    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it)
                                    {
                                        const double anchor =
                                            mode == QStringLiteral("start") ? it.value().first
                                          : mode == QStringLiteral("end")   ? it.value().second
                                                                            : (it.value().first + it.value().second) / 2.0;
                                        const double target =
                                            mode == QStringLiteral("start") ? gLo
                                          : mode == QStringLiteral("end")   ? gHi
                                                                            : (gLo + gHi) / 2.0;
                                        const double off = target - anchor;
                                        m_appCtx.setSourceTimeOffset(it.key(), off);
                                        applied.insert(QString::number(it.key()), off);
                                    }
                                    return QVariant(applied);
                                });

    // 回放引擎（对齐 PJ4 PlaybackEngine）：会话级时间游标的播放/暂停/定位/速率/循环/步进。
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.play"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->play();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.pause"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->pause();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->togglePlay();
                                    return QVariant(m_playback->isPlaying());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.seek"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->setCurrentTime(params.value(QStringLiteral("t")).toDouble());
                                    return QVariant(m_playback->currentTime());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.rate"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->setRate(params.value(QStringLiteral("rate")).toDouble());
                                    return QVariant(m_playback->rate());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.loop"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    const bool on = params.contains(QStringLiteral("on"))
                                        ? params.value(QStringLiteral("on")).toBool() : !m_playback->isLooping();
                                    m_playback->setLooping(on);
                                    return QVariant(m_playback->isLooping());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.step"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    bool dirOk = false;
                                    const int dir = params.value(QStringLiteral("dir")).toInt(&dirOk);
                                    const int stepDir = dirOk ? dir : 1;
                                    m_playback->setCurrentTime(m_playback->currentTime() + stepDir * m_playback->step());
                                    return QVariant(m_playback->currentTime());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.state"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    return QVariant(QVariantMap{
                                        {QStringLiteral("t"), m_playback->currentTime()},
                                        {QStringLiteral("min"), m_playback->rangeMin()},
                                        {QStringLiteral("max"), m_playback->rangeMax()},
                                        {QStringLiteral("rate"), m_playback->rate()},
                                        {QStringLiteral("playing"), m_playback->isPlaying()},
                                        {QStringLiteral("looping"), m_playback->isLooping()},
                                    });
                                });

    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (activeChannelView())
                                        activeChannelView()->clearChannels();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.list"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantList list;
                                    if (PlotCanvas *c = activeCanvas())
                                        for (const PlotSeries &s : c->series())
                                            list.append(QVariantMap{
                                                {QStringLiteral("key"), s.key()},
                                                {QStringLiteral("dataset"), s.datasetId},
                                                {QStringLiteral("table"), s.table},
                                                {QStringLiteral("column"), s.column},
                                                {QStringLiteral("points"), s.lod.size()},
                                                {QStringLiteral("rightAxis"), s.rightAxis},
                                            });
                                    return QVariant(list);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.axis"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool right = params.value(QStringLiteral("right")).toBool();
                                    if (key.isEmpty())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesAxis(key, right))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    // 图型切换：workspace 直接处理（曲线在画布切、专用图型在 cell 内切页），
    // 不再需要外壳 requestPanel 替换整个中央——多画布形态得以保留。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.setType"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString kind = params.value(QStringLiteral("type")).toString();
                                    const bool handled = setCellType(m_active, kind);
                                    QVariantMap r;
                                    r.insert(QStringLiteral("handled"), handled);
                                    r.insert(QStringLiteral("kind"), kind);
                                    return QVariant(r);
                                });

    // —— plotting 全套操作意图（抄老 PlottingRibbon / PlottingCanvas 的体验）——
    // 全部落到 active cell 的 PlotCanvas 真实能力，不造空按钮。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.visible"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool vis = params.value(QStringLiteral("visible"), true).toBool();
                                    if (key.isEmpty())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesVisible(key, vis))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.color"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const QColor color = params.value(QStringLiteral("color")).value<QColor>();
                                    if (key.isEmpty() || !color.isValid())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key 或颜色"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesInk(key, color))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.remove"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    if (key.isEmpty())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->removeSeries(key))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.zoom"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const double factor = params.value(QStringLiteral("factor"), 0.5).toDouble();
                                    if (!activeCanvas())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    activeCanvas()->zoomRange(factor <= 0.0 ? 0.5 : factor);
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.view.undo"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->canUndoView())
                                        return QVariant(false);
                                    c->undoView();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.autoscale"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!activeCanvas())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    activeCanvas()->autoScale();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    // axis 省略=整体开关；"x"=竖网格（Bottom 轴刻度）；"y"=横网格。
                                    auto *ax = c->plotWidget();
                                    const QString axis = params.value(QStringLiteral("axis")).toString();
                                    const bool on = params.contains(QStringLiteral("on"))
                                                        ? params.value(QStringLiteral("on")).toBool()
                                                        : !ax->gridVisible();
                                    if (axis.compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0)
                                        ax->setAxisGridVisible(fastplot::AxisSide::Bottom, on);
                                    else if (axis.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0)
                                    {
                                        ax->setAxisGridVisible(fastplot::AxisSide::Left, on);
                                        ax->setAxisGridVisible(fastplot::AxisSide::Right, on);
                                    }
                                    else
                                        ax->grid(on);
                                    return QVariant(on);
                                });
    // 显示组「网格」下拉的勾选态：返回当前活动画布的 X/Y 网格线可见性。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.grid.query"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    auto *ax = c->plotWidget();
                                    QVariantMap out{{QStringLiteral("x"),
                                                     ax->axisGridVisible(fastplot::AxisSide::Bottom)},
                                                    {QStringLiteral("y"),
                                                     ax->axisGridVisible(fastplot::AxisSide::Left)
                                                         || ax->axisGridVisible(fastplot::AxisSide::Right)}};
                                    return QVariant(out);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.box"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    const bool on = params.contains(QStringLiteral("on"))
                                                        ? params.value(QStringLiteral("on")).toBool()
                                                        : !c->plotWidget()->boxVisible();
                                    c->plotWidget()->box(on);
                                    return QVariant(on);
                                });
    // 老 buildPanelsToolbar 的工具模式：移动/放大/缩小左键互斥（-1=恢复默认）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.tool"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int tool = params.value(QStringLiteral("tool"), -1).toInt();
                                    for (const Cell &cell : m_cells)
                                    {
                                        PlotCanvas *c = cell.canvas;
                                        if (!c || !c->zoomer() || !c->panner())
                                            continue;
                                        auto *z = c->zoomer();
                                        auto *pn = c->panner();
                                        z->setLeftBand(tool == 1 || tool == 2);
                                        z->setBandZoomOut(tool == 2);
                                        pn->setEnabled(tool == 0 || tool == -1);
                                    }
                                    return QVariant(tool);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.axis.range"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    const QString axis = params.value(QStringLiteral("axis"), QStringLiteral("x")).toString();
                                    const double lo = params.value(QStringLiteral("lo")).toDouble();
                                    const double hi = params.value(QStringLiteral("hi")).toDouble();
                                    if (hi <= lo)
                                        return fail(QStringLiteral("erange"), QStringLiteral("范围非法"));
                                    if (axis == QStringLiteral("y2"))
                                        c->plotWidget()->setYRange2(lo, hi);
                                    else if (axis == QStringLiteral("y"))
                                        c->plotWidget()->setYRange(lo, hi);
                                    else
                                        c->plotWidget()->setXRange(lo, hi);
                                    c->plotWidget()->update();
                                    return QVariant(true);
                                });
    // 文本读数覆盖层：时间标记 / A-B 测量 / 播放头共用，诚实呈现为读数条
    // （老代码那套交互式拖拽游标是 P2，这里不伪装成已实现）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.overlay"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    const QString text = params.value(QStringLiteral("text")).toString();
                                    if (text.isEmpty())
                                        c->plotWidget()->clearOverlay();
                                    else
                                        c->plotWidget()->setOverlayText(text);
                                    return QVariant(true);
                                });

    // —— page.*：页面/画布布局操作（抄老 page 面板体验）——
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.addCanvas"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(addCell());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.removeCanvas"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(removeAt(m_active));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.splitHorizontal"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(split(true));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.splitVertical"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(split(false));
                                });
    // 页签复制：对齐老 PlottingTabs 的"复制页"——整页搬走（页内每一格的曲线与视图状态一起）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.duplicate"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!duplicatePage())
                                        return fail(QStringLiteral("epage"), QStringLiteral("复制页失败"));
                                    return QVariant(pageCount());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.fullscreen"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    toggleFullscreen();
                                    return QVariant(m_fullscreenCell >= 0);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int r = params.value(QStringLiteral("rows")).toInt();
                                    const int c = params.value(QStringLiteral("cols")).toInt();
                                    if (!setGrid(r, c))
                                        return fail(QStringLiteral("egrid"), QStringLiteral("网格规格非法"));
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols}};
                                    return QVariant(out);
                                });
    // 老 buildLayoutPicker 的 m×n 选择器：点「网格」弹行列输入，再走 page.grid 重排。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.grid.pick"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    bool ok = false;
                                    const QString input = QInputDialog::getText(
                                        this, QStringLiteral("网格布局"),
                                        QStringLiteral("输入行×列（如 2×1、3×2，最大 6×6）："),
                                        QLineEdit::Normal,
                                        QStringLiteral("%1×%2").arg(m_rows).arg(m_cols), &ok);
                                    if (!ok || input.isEmpty())
                                        return QVariant(false);
                                    // 同时接受 × x X * 分隔符。
                                    QString norm = input;
                                    for (const QChar sep : {QLatin1Char('x'), QLatin1Char('X'), QLatin1Char('*')})
                                        norm.replace(sep, QLatin1Char('\u00d7'));
                                    const QStringList parts = norm.split(QLatin1Char('\u00d7'));
                                    if (parts.size() != 2)
                                        return fail(QStringLiteral("egrid"), QStringLiteral("格式应为 行×列"));
                                    int r = parts.at(0).trimmed().toInt();
                                    int c = parts.at(1).trimmed().toInt();
                                    if (r < 1 || r > 6 || c < 1 || c > 6)
                                        return fail(QStringLiteral("egrid"), QStringLiteral("行列须在 1~6 之间"));
                                    if (!setGrid(r, c))
                                        return fail(QStringLiteral("egrid"), QStringLiteral("网格规格非法"));
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("rows"), r}, {QStringLiteral("cols"), c}};
                                    return QVariant(out);
                                });
    // 老 PlottingRibbon"页面"组的"新建页"：真加一个页签，页内先给一块空画布
    //（对齐老 appendPlusTab 的"+"页行为，而不是把当前页重置）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.new"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    const int index = addPage();
                                    m_tabs->setCurrentIndex(index);   // 触发 switchPage 载入新页
                                    switchPage(index);                // 兜底（同索引无副作用）
                                    if (m_cells.isEmpty())
                                        addCell();                    // 新页先给一格空画布
                                    return QVariant(pageCount());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.closeCurrent"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(removePage(m_activePage));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.rename"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int index = params.contains(QStringLiteral("index"))
                                        ? params.value(QStringLiteral("index")).toInt()
                                        : m_activePage;
                                    const QString title = params.value(QStringLiteral("title")).toString();
                                    if (index < 0 || index >= m_pages.size() || title.isEmpty())
                                        return fail(QStringLiteral("epage"), QStringLiteral("页签重命名参数非法"));
                                    m_pages.at(index)->title = title;
                                    m_tabs->setTabText(index, title);
                                    return QVariant(title);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.select"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int index = params.value(QStringLiteral("index")).toInt();
                                    if (index < 0 || index >= m_pages.size())
                                        return fail(QStringLiteral("epage"), QStringLiteral("页索引越界"));
                                    m_tabs->setCurrentIndex(index);
                                    switchPage(index);
                                    return QVariant(index);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.state"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("active"), m_active},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols},
                                                    {QStringLiteral("pages"), pageCount()},
                                                    {QStringLiteral("activePage"), m_activePage}};
                                    return QVariant(out);
                                });
}

QWidget *createCanvasWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
{
    return new CanvasWorkspace(appCtx, executor, parent);
}

// 视图层自注册：中央工作区，替代单画布承载多画布 grid。
NG_REGISTER_PANEL("canvas-workspace", canvasWorkspace, createCanvasWorkspace)

} // namespace nextpilot::plotscope::gui

