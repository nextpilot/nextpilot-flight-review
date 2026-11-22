#include "CanvasWorkspace.h"

#include "AppContext.h"
#include "ChannelView.h"
#include "GridPickPopup.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryHub.h"
#include "PlotCanvas.h"
#include "PlaybackEngine.h"
#include "TimelineWidget.h"
#include "DatasetHub.h"
#include "Dataset.h"
#include "SourceHub.h"
#include "PropertySchema.h"
#include "Result.h"
#include "ErrorCodes.h"
#include "Transform.h"
#include "TransformEditorDialog.h"
#include <fastplot/Axes.h>
#include <fastplot/Panner.h>
#include <fastplot/Zoomer.h>

#include <limits>

// 布局层：每格一个 ADS 停靠面板（拖动分屏 / 拖到中心合并 / 拖出浮动 / 拖回停靠全由 ADS 提供）。
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockComponentsFactory.h"
#include "DockContainerWidget.h"
#include "DockManager.h"
#include "DockSplitter.h"
#include "DockWidget.h"

#include <QEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QGuiApplication>
#include <QToolTip>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QScrollBar>
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

// 每格标题条按钮的图标：资源缺失时返回空 QIcon（按钮退化为文字，不刷告警）。
QIcon cellIcon(const char *name)
{
    const QString path = QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name));
    return QFile::exists(path) ? QIcon(path) : QIcon();
}

// 原生标题栏一律藏起来：标题与按钮由每格自己的标题条（makeHoverBar）承担，
// 但对象仍要存在——拖动由鼠标事件转发给它，ADS 才会起拖（PJ4 的 DockToolbar 同一手法）。
class HiddenCellTitleBar : public ads::CDockAreaTitleBar
{
public:
    using ads::CDockAreaTitleBar::CDockAreaTitleBar;
    void setVisible(bool) override { QWidget::setVisible(false); }
};

class HiddenCellTitleBarFactory : public ads::CDockComponentsFactory
{
public:
    ads::CDockAreaTitleBar *createDockAreaTitleBar(ads::CDockAreaWidget *dockArea) const override
    {
        auto *titleBar = new HiddenCellTitleBar(dockArea);
        titleBar->setVisible(false);
        return titleBar;
    }
};
} // namespace

CanvasWorkspace::CanvasWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_executor(executor)
{
    // 外层纵向布局：网格画布区 + 底部时间条。
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 页签容器：一页 = 一套绘图布局（对齐老 PlottingTabs）。
    // 可移动/可关闭，但至少保留一页（removePage 里挡）。
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    // 页序 = 绘图页序，拖拽改序会让页索引与页对象错位，故不可拖。
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

    // 默认一页，页内一个画布 cell。
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
    // 每格一个外框：竖排 [标题条, 画布堆叠]。停靠面板装的是这个外框，
    // 标题条因此固定在格子顶部，既是按钮区也是拖动把手（PJ4 DockToolbar 的形态）。
    auto *frame = new QWidget(parentForStack);
    frame->setObjectName(QStringLiteral("plotCellFrame"));
    auto *frameLay = new QVBoxLayout(frame);
    frameLay->setContentsMargins(0, 0, 0, 0);
    frameLay->setSpacing(0);

    auto *stack = new QStackedWidget(frame);
    auto *canvas = new PlotCanvas(stack);
    canvas->setCanvasId(++m_canvasSeq);
    auto *view = new ChannelView(m_appCtx.transforms(), m_executor, canvas, canvas);

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
    // 曲线从一个画布拖到另一个画布：目标是"接收方"，源按 MIME 里的 canvasId 回找。
    // 先让目标加上，成功后才删源——目标失败（比如数据集已卸载）时源不受影响，
    // 也就不会出现"两边都没有"的半截状态。
    QObject::connect(canvas, &PlotCanvas::seriesTransferRequested, this,
                     [this](int srcCanvasId, const QString &key, int datasetId,
                            const QString &table, const QString &column, const QString &title) {
                         auto *targetCanvas = qobject_cast<PlotCanvas *>(sender());
                         if (!targetCanvas || targetCanvas->canvasId() == srcCanvasId)
                             return;
                         ChannelView *targetView = viewOfCanvas(targetCanvas);
                         PlotCanvas *srcCanvas = canvasById(srcCanvasId);
                         ChannelView *srcView = viewOfCanvas(srcCanvas);
                         if (!targetView || !srcView)
                             return; // 源不在当前页（跨页移动未支持）：拒绝，源保持原样
                         if (!targetView->addChannel(datasetId, table, column, nullptr, title))
                             return;
                         // 移动不是复制：目标成功后才摘源。
                         srcView->removeChannel(datasetId, table, column);
                         Q_UNUSED(key);
                     });
    // 画布选中请求 -> 统一 selection（属性面板据此刷新）。
    connectCanvasSignals(canvas);
    // 时间轴刻度按设置初始化：新格沿用当前全局偏好。
    canvas->setTimeFormatMode(m_appCtx.settings()
                                  .value(QStringLiteral("plot.time.format"), QStringLiteral("relative"))
                                  .toString());

    Cell cell;
    cell.canvas = canvas;
    cell.view = view;
    cell.instrument = instrument;
    cell.stack = stack;
    cell.frame = frame;
    // 每格的标题条：常显在格子顶部（不再"鼠标进才浮出"），按钮直接作用在"这一格"。
    cell.hoverBar = makeHoverBar(cell, frame);
    frameLay->addWidget(cell.hoverBar);
    frameLay->addWidget(stack, 1);
    // 图例从上往下让出标题条的高度，曲线不与标题条贴边。
    if (auto *ax = canvas->plotForTest())
        ax->setLegendTopInset(28.0);
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
    // 于是这一格顶部看起来就是一条标题栏：左端格名，右端按钮。
    QColor barBg = parentStack->palette().alternateBase().color();
    barBg.setAlpha(255);
    bar->setFixedHeight(fastplot::Axes::kTitleBarHeight - 2);
    bar->setStyleSheet(QStringLiteral(
        "#cellHoverBar { background: %1; }"
        " QToolButton { border: none; padding: 0px; border-radius: 2px; background: transparent; }"
        " QToolButton:hover { background: rgba(127,127,127,0.28); }"
        " QLabel { background: transparent; color: palette(text); }")
        .arg(barBg.name(QColor::HexArgb)));

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 0, 2, 0);
    lay->setSpacing(0);

    // 标题条空白处也能拖这一格：条形本身接鼠标事件，再转给 ADS 起拖。
    bar->installEventFilter(this);
    // 格名：像 PJ4 的绘图标题一样常显；双击可改名，按住拖动就是搬这一格。
    auto *title = new QLabel(QStringLiteral("图"), bar);
    title->setToolTip(QStringLiteral("拖动这里可重新排列；双击改名"));
    title->setCursor(Qt::OpenHandCursor);
    title->installEventFilter(this);
    cell.titleLabel = title;

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

    lay->addWidget(title, 1);
    lay->addWidget(leftRight);
    lay->addWidget(topBottom);
    lay->addWidget(maxBtn);
    lay->addWidget(bookmarkBtn);
    lay->addWidget(abBtn);
    lay->addWidget(closeBtn);
    // 标题条常显（与 PJ4 的 per-dock 工具条一致），不再靠鼠标进出控制显隐。
    bar->setAttribute(Qt::WA_Hover, true);
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
    // 标题条现在由外框的竖直布局摆放（固定在上沿），不再需要手工定位。
    Q_UNUSED(cell);
}

int CanvasWorkspace::indexOfHoverBar(const QObject *bar) const
{
    if (!bar)
        return -1;
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells.at(i).hoverBar == bar || m_cells.at(i).titleLabel == bar)
            return i;
    return -1;
}

void CanvasWorkspace::showHoverBar(int index)
{
    // 标题条常显：这里只承担"把这一格设为活动"的语义（命令面板/截图调用时仍可定位到它）。
    if (index < 0 || index >= m_cells.size())
        return;
    setActive(index);
}

void CanvasWorkspace::hideHoverBars()
{
    // 标题条常显，不再自动收起（收起来就没有拖动把手了）。
}

bool CanvasWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // 这一格内的对象（外框 / 堆叠 / 画布 / 仪表 / 标题条）都被装了过滤器：统一映射回 cell 索引。
    int cellIndex = -1;
    for (int i = 0; i < m_cells.size(); ++i)
    {
        const Cell &c = m_cells.at(i);
        if (watched == c.frame || watched == c.stack || watched == c.canvas || watched == c.instrument
            || (c.hoverBar && watched == c.hoverBar) || (c.titleLabel && watched == c.titleLabel))
        {
            cellIndex = i;
            break;
        }
    }

    if (cellIndex >= 0)
    {
        const Cell &cell = m_cells.at(cellIndex);

        if ((type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease)
            && (watched == cell.hoverBar || watched == cell.titleLabel))
        {
            setActive(cellIndex);
            // 标题条就是拖动把手：按下/抬起都转给 ADS 隐藏的原生标题栏，由它起拖与落位。
            // 位置给 (0,0)，浮动预览才会贴着光标而不是按你按在标题条的位置偏移。
            ads::CDockWidget *dockWidget = cell.dockWidget;
            if (dockWidget && dockWidget->dockAreaWidget() && dockWidget->dockAreaWidget()->titleBar())
            {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                QMouseEvent forwarded(type, QPointF(0, 0), mouseEvent->globalPosition(), mouseEvent->button(),
                                      mouseEvent->buttons(), mouseEvent->modifiers());
                QCoreApplication::sendEvent(dockWidget->dockAreaWidget()->titleBar(), &forwarded);
                return true;
            }
        }
        else if (type == QEvent::MouseButtonDblClick && watched == cell.titleLabel)
        {
            // 双击格名改名（与绘图页签双击改名同一习惯）。
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, QStringLiteral("重命名画布"), QStringLiteral("名称"), QLineEdit::Normal,
                cell.titleLabel ? cell.titleLabel->text() : QString(), &ok);
            const QString trimmed = name.trimmed();
            if (ok && !trimmed.isEmpty() && cell.titleLabel)
            {
                cell.titleLabel->setText(trimmed);
                if (cell.dockWidget)
                    cell.dockWidget->setWindowTitle(trimmed);
            }
            return true;
        }
        else if (type == QEvent::MouseButtonPress)
        {
            setActive(cellIndex);
        }
        else if (type == QEvent::Enter && watched == cell.stack)
        {
            setActive(cellIndex);          // 指针进到这一格 → 它成为活动格（标题条本来就常显）
        }
    }

    return QWidget::eventFilter(watched, event);
}

void CanvasWorkspace::setActive(int index)
{
    if (index < 0 || index >= m_cells.size() || m_active == index)
        return;
    m_active = index;
}

PlotCanvas *CanvasWorkspace::canvasById(int id) const
{
    if (id < 0)
        return nullptr;
    for (const Cell &c : m_cells)
        if (c.canvas && c.canvas->canvasId() == id)
            return c.canvas;
    return nullptr;
}

ChannelView *CanvasWorkspace::viewOfCanvas(const PlotCanvas *canvas) const
{
    if (!canvas)
        return nullptr;
    for (const Cell &c : m_cells)
        if (c.canvas == canvas)
            return c.view;
    return nullptr;
}

void CanvasWorkspace::detachStacks()
{
    // 重建停靠树前必须先把每格的外框移出去：删面板会删掉它持有的子控件，
    // 而外框（含标题条与画布）归本类所有（还要复用），不能被连带删除。
    for (Cell &c : m_cells)
    {
        if (!c.frame)
            continue;
        if (c.dockWidget)
        {
            // takeWidget 把控件从面板里摘出来（面板随后销毁不会再连带删它）。
            if (QWidget *taken = c.dockWidget->takeWidget())
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

ads::CDockWidget *CanvasWorkspace::makeDockForCell(Cell &cell, int side, ads::CDockWidget *relative,
                                                  bool intoContainer)
{
    Page *page = m_pages.isEmpty() ? nullptr : m_pages.at(m_activePage);
    if (!page || !page->dock || !cell.frame)
        return nullptr;

    // 面板标题按当前格序编号（"图 3"），拖动时在浮动窗标题栏上也看得见。
    int ordinal = 1;
    for (int i = 0; i < m_cells.size(); ++i)
    {
        if (m_cells.at(i).frame == cell.frame)
        {
            ordinal = i + 1;
            break;
        }
    }
    // 用户改过名的格保留自己的名字，没改的跟着格序走。
    const QString title = (cell.titleLabel && cell.titleLabel->text() != QStringLiteral("图"))
                              ? cell.titleLabel->text()
                              : QStringLiteral("图 %1").arg(ordinal);
    if (cell.titleLabel)
        cell.titleLabel->setText(title);

    auto *dockWidget = new ads::CDockWidget(page->dock, title);
    dockWidget->setObjectName(QStringLiteral("plotCellDock"));
    // 关闭入口留在每格的标题条上（面板自带关闭按钮会和它重复，还可能被误点）。
    dockWidget->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    // 可拖、可浮动：拖动分屏 / 拖到中心合并 / 拖出成浮动窗 / 拖回停靠，全部由 ADS 提供。
    dockWidget->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
    // 装的是"外框"（标题条 + 画布），画布要填满面板，不让 ADS 再套一层滚动区。
    dockWidget->setWidget(cell.frame, ads::CDockWidget::ForceNoScrollArea);
    cell.dockWidget = dockWidget;

    const ads::DockWidgetArea area = (side == 0)  ? ads::CenterDockWidgetArea
                                    : (side == 1) ? ads::RightDockWidgetArea
                                                  : ads::BottomDockWidgetArea;
    ads::CDockAreaWidget *areaWidget = nullptr;
    if (intoContainer)
    {
        // 相对整个页容器插入：整宽换行（第一列之外的行靠它，行内再往右拆）。
        areaWidget = page->dock->addDockWidgetToContainer(area, dockWidget, page->dock);
    }
    else if (relative)
    {
        areaWidget = page->dock->addDockWidget(area, dockWidget, relative->dockAreaWidget());
    }
    else
    {
        areaWidget = page->dock->addDockWidget(area, dockWidget);
    }
    if (areaWidget)
        areaWidget->setAllowedAreas(ads::OuterDockAreas);

    cell.frame->show();
    return dockWidget;
}

int CanvasWorkspace::indexOfDock(const ads::CDockWidget *dockWidget) const
{
    if (!dockWidget)
        return -1;
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells.at(i).dockWidget == dockWidget)
            return i;
    return -1;
}

void CanvasWorkspace::equalizeSplitters()
{
    Page *page = m_pages.isEmpty() ? nullptr : m_pages.at(m_activePage);
    if (!page || !page->dock)
        return;

    // 严格等分：ADS 的分隔器按相对权重分配空间，所有项给同一权重即等宽/等高
    // （拖分隔条时权重被改写，点 m×n 后由这里重新抹平）。
    auto apply = [page]() {
        if (!page->dock)
            return;
        const auto splitters = page->dock->findChildren<ads::CDockSplitter *>();
        for (auto *splitter : splitters)
        {
            const int count = splitter->count();
            if (count <= 0)
                continue;
            QList<int> sizes;
            sizes.reserve(count);
            for (int i = 0; i < count; ++i)
                sizes.append(1000000 / count);
            splitter->setSizes(sizes);
        }
    };
    // 首次布局时控件尺寸还是 0，等事件循环走完再抹平一次。
    apply();
    QTimer::singleShot(0, this, apply);
}

void CanvasWorkspace::rebuildPageLayout()
{
    if (m_pages.isEmpty())
        return;
    Page &page = *m_pages[m_activePage];
    if (!page.host)
        return;

    // 重排会重建面板：全屏时被隐藏的其它面板先放回来，否则它们的隐藏记录就悬空了。
    for (const QPointer<ads::CDockWidget> &dockWidget : m_fullscreenHidden)
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
    m_rebuildingDock = true;
    if (page.dock)
    {
        delete page.dock;
        page.dock = nullptr;
    }
    for (Cell &c : m_cells)
        c.dockWidget = nullptr;

    page.dock = new ads::CDockManager(page.host);
    page.dock->setObjectName(QStringLiteral("plotPageDock"));
    // 每格自己的标题条负责显示与按钮，ADS 原生标题栏一律藏起来（对象仍保留给拖动用）。
    page.dock->setComponentsFactory(new HiddenCellTitleBarFactory());
    // 画布区自绘标题栏/样式由主题统一管，先关掉 ADS 自带样式表免得打架。
    page.dock->setStyleSheet(QString());
    if (auto *lay = qobject_cast<QVBoxLayout *>(page.host->layout()))
        lay->addWidget(page.dock);
    else
    {
        auto *lay2 = new QVBoxLayout(page.host);
        lay2->setContentsMargins(0, 0, 0, 0);
        lay2->setSpacing(0);
        lay2->addWidget(page.dock);
    }

    // 点/拖动某格即成为活动格（view action 都路由到它）。
    QObject::connect(page.dock, &ads::CDockManager::focusedDockWidgetChanged, this,
                     [this](ads::CDockWidget *, ads::CDockWidget *now) {
                         const int index = indexOfDock(now);
                         if (index >= 0)
                             setActive(index);
                     });

    // 用户点面板自带的关闭：把这一格一起收掉，别在 m_cells 里留空指针。
    // 重建旧树时 ADS 也会发这个信号（m_rebuildingDock 期间一律忽略）。
    QObject::connect(page.dock, &ads::CDockManager::dockWidgetRemoved, this,
                     [this](ads::CDockWidget *removed) {
                         if (m_rebuildingDock || !removed)
                             return;
                         const int index = indexOfDock(removed);
                         if (index < 0)
                             return;
                         Cell cell = m_cells.takeAt(index);
                         if (QWidget *taken = removed->takeWidget())
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
                     });

    const int cols = std::max(1, m_cols);
    const int rows = std::max(1, m_rows);
    // 对齐老 PlottingLayout：行=横向排列，行与行上下叠；行内每格等宽、各行等高
    // （等分在布局完成后由 equalizeSplitters() 统一抹平）。
    int index = 0;
    ads::CDockWidget *prevInRow = nullptr;
    for (int r = 0; r < rows; ++r)
    {
        prevInRow = nullptr;
        for (int c = 0; c < cols && index < m_cells.size(); ++c, ++index)
        {
            Cell &cell = m_cells[index];
            ads::CDockWidget *dockWidget = nullptr;
            if (r == 0 && c == 0)
                dockWidget = makeDockForCell(cell, 0);                       // 首格：占住整页
            else if (r == 0)
                dockWidget = makeDockForCell(cell, 1, prevInRow);            // 首行：往右排
            else if (c == 0)
                dockWidget = makeDockForCell(cell, 2, nullptr, true);        // 换行：整宽插入页容器下方
            else
                dockWidget = makeDockForCell(cell, 1, prevInRow);            // 行内往右
            if (!dockWidget)
                break;
            prevInRow = dockWidget;
        }
    }

    page.gridMode = true;
    m_rebuildingDock = false;
    // 网格模式下每一级分隔器都等分：默认按 sizeHint 分配会让空白画布被挤成一条。
    equalizeSplitters();
}

int CanvasWorkspace::reflowGrid(int rows, int cols)
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
            if (QWidget *taken = dropped.dockWidget->takeWidget())
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

    // 页容器：不再套滚动区——停靠面板要能被拖出/拖回、浮动窗也要对得准，
    // 塞在滚动区里拖动会错位（PJ4 的画面区同样不滚动）。格子多时由分隔器压缩，
    // 想看单格用最大化。
    page->host = new QWidget(this);
    page->host->setObjectName(QStringLiteral("plotPage"));
    auto *lay = new QVBoxLayout(page->host);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(3);
    m_pages.append(page);

    // 永远排在末尾「＋」之前，保证 tab 索引 0..n-1 与 m_pages 一一对应。
    const int plusIndex = qMax(0, m_tabs->count() - 1);
    const int index = m_tabs->insertTab(plusIndex, page->host, page->title);
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
    // 切页前收起当前页的全屏：隐藏的面板属于旧页，留着切回来就是一片空白。
    if (m_fullscreenCell >= 0)
    {
        for (const QPointer<ads::CDockWidget> &dockWidget : m_fullscreenHidden)
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

bool CanvasWorkspace::removePage(int index)
{
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
    m_tabs->blockSignals(true);
    m_tabs->removeTab(index);
    m_tabs->blockSignals(false);
    m_tabs->setCurrentIndex(m_activePage);

    delete page->host;     // 连同页内 dock 容器与 cell 控件一起销毁
    delete page;
    return true;
}

bool CanvasWorkspace::removeAt(int index)
{
    if (index < 0 || index >= m_cells.size() || m_cells.size() <= 1)
        return false;
    Cell cell = m_cells.takeAt(index);
    // 先把它从停靠树里摘掉（面板壳随删除一起销毁），再销毁内容。
    if (cell.dockWidget)
    {
        if (!m_pages.isEmpty())
            if (Page *page = m_pages.at(m_activePage); page != nullptr && page->dock)
                page->dock->removeDockWidget(cell.dockWidget);
        if (QWidget *taken = cell.dockWidget->takeWidget())
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

bool CanvasWorkspace::split(bool horizontal)
{
    if (m_cells.isEmpty())
        return false;
    const int anchorIndex = qBound(0, m_active, m_cells.size() - 1);
    ads::CDockWidget *anchor = m_cells.at(anchorIndex).dockWidget;
    if (!anchor)
        return false;

    // 真正的"格内再拆"（对齐 PJ4 的 splitHorizontal/splitVertical）：新面板停靠到
    // 活动格的右/下方，只切这一块，其它格尺寸不动。
    Cell cell = makeCell(nullptr);
    const int insertAt = anchorIndex + 1;
    m_cells.insert(insertAt, cell);
    ads::CDockWidget *created = makeDockForCell(m_cells[insertAt], horizontal ? 1 : 2, anchor);
    if (!created)
    {
        // 停靠失败：回滚，不留半截状态。
        Cell failed = m_cells.takeAt(insertAt);
        if (failed.frame)
            failed.frame->deleteLater();
        return false;
    }
    m_active = insertAt;
    // 拆过之后不再是规整网格：网格规格交给「m×n」重排时重算。
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
    // 退出：把全屏时藏起来的其它面板放回来（只放这一批，用户自己关掉的不会被重开）。
    if (m_fullscreenCell >= 0)
    {
        for (const QPointer<ads::CDockWidget> &dockWidget : m_fullscreenHidden)
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
        ads::CDockWidget *other = m_cells.at(i).dockWidget;
        if (!other)
            continue;
        other->toggleView(false);
        m_fullscreenHidden.append(other);
    }
    m_fullscreenCell = index;
    syncPage();
}

bool CanvasWorkspace::setGrid(int rows, int cols)
{
    if (rows < 1 || cols < 1)
        return false;
    // 一条命令解决三件事：剔掉没曲线的空白格、容量不够自动加行、按 m×n 严格等分重排。
    reflowGrid(rows, cols);
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
    // 右键"拆分 / 删除此画布"：按指针反查所在格再执行。
    // 先 setActive 是必须的——右键的画布很可能不是当前激活格，
    // 直接调 split()/removeAt(m_active) 会作用到别的画布上。
    QObject::connect(canvas, &PlotCanvas::canvasActionRequested, this,
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
    QObject::connect(canvas, &PlotCanvas::seriesTransformRequested, this, [this, canvas](const QString &key) {
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
            ? m_appCtx.transforms().get(s->yTransformId).label : QStringLiteral("无");
        tr.rows << row(QStringLiteral("yTransform"), QStringLiteral("Y 轴变换"), yDesc,
                       PropKind::Text, true);
        tr.rows.last().tooltip = QStringLiteral("可选算子/表达式/Lua；在右键菜单或脚本里用 "
                                                "plot.series.transform 设置（能力：轴Y）。可选清单见 "
                                                "transforms.idsByCapability(CapAxisY)");
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
            : m_appCtx.transforms().get(tfId).label;
        if (axis == QStringLiteral("x"))
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("X 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("X 轴变换 id。可选：transforms.idsByCapability(CapAxisX)，"
                                                    "如 builtin.scale 的 time_offset_sec / expression");
        }
        else
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("Y 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("Y 轴变换 id。可选：transforms.idsByCapability(CapAxisY)");
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
                // 轴变换通过 TransformHub 的 id 生效；实际执行在曲线内（Task #123）。
                if (!value.isEmpty() && !m_appCtx.transforms().contains(value))
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
    // 时间轴刻度三态：设置保存后外壳派发一次，所有格的画布立即按新格式重绘。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.timeFormat"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    QString mode = params.value(QStringLiteral("mode")).toString();
                                    if (mode.isEmpty())
                                        mode = m_appCtx.settings()
                                                   .value(QStringLiteral("plot.time.format"),
                                                          QStringLiteral("relative"))
                                                   .toString();
                                    // 遍历**所有页**的 cell：非当前页的 cells 存在 Page 里，
                                    // 只扫 m_cells 会让切页后的画布沿用旧刻度（用户会当成"设置没生效"）。
                                    int applied = 0;
                                    QSet<PlotCanvas *> seen;
                                    auto applyOne = [&](const Cell &c) {
                                        if (!c.canvas || seen.contains(c.canvas))
                                            return;
                                        seen.insert(c.canvas);
                                        c.canvas->setTimeFormatMode(mode);
                                        c.canvas->update();
                                        ++applied;
                                    };
                                    for (int pi = 0; pi < m_pages.size(); ++pi)
                                    {
                                        const QVector<Cell> &cells =
                                            (pi == m_activePage) ? m_cells : m_pages.at(pi)->cells;
                                        for (const Cell &c : cells)
                                            applyOne(c);
                                    }
                                    for (const Cell &c : m_cells) // 防御：页表尚未写回时也算上
                                        applyOne(c);
                                    return QVariant(applied);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.export.png"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(err::Path, QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportPng(path))
                                        return fail(err::Io, QStringLiteral("PNG 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.export.csv"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(err::Path, QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportCsv(path))
                                        return fail(err::Io, QStringLiteral("CSV 导出失败"));
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
                                        return fail(err::State, QStringLiteral("视图状态复原失败"));
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

    // 状态视图：rows = [{channel, segments:[…]}]，
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

    // === 图例交互（第二梯队 #205，
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

    // === 多源时间对齐（第二梯队 #204，
    // 源级显示时间偏移（秒）：施加在"曲线 x + 源 offset"，不改数据。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.offset.set"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    bool okId = false;
                                    const int src = params.value(QStringLiteral("sourceId")).toInt(&okId);
                                    if (!okId || src < 0)
                                        return Error{err::Args,
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
                                        return Error{err::Args,
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
                                        return Error{err::NoData,
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

    // 回放引擎：会话级时间游标的播放/暂停/定位/速率/循环/步进。
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
                                        return fail(err::Key, QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesAxis(key, right))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
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

    // —— plotting 全套操作意图 ——
    // 全部落到 active cell 的 PlotCanvas 真实能力，不造空按钮。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.visible"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool vis = params.value(QStringLiteral("visible"), true).toBool();
                                    if (key.isEmpty())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesVisible(key, vis))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.color"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const QColor color = params.value(QStringLiteral("color")).value<QColor>();
                                    if (key.isEmpty() || !color.isValid())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key 或颜色"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesInk(key, color))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.remove"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    if (key.isEmpty())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->removeSeries(key))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.zoom"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const double factor = params.value(QStringLiteral("factor"), 0.5).toDouble();
                                    if (!activeCanvas())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
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
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    activeCanvas()->autoScale();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
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
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
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
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
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
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    const QString axis = params.value(QStringLiteral("axis"), QStringLiteral("x")).toString();
                                    const double lo = params.value(QStringLiteral("lo")).toDouble();
                                    const double hi = params.value(QStringLiteral("hi")).toDouble();
                                    if (hi <= lo)
                                        return fail(err::Range, QStringLiteral("范围非法"));
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
    // （交互式拖拽游标未实现，此处仅做读数条）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.overlay"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    const QString text = params.value(QStringLiteral("text")).toString();
                                    if (text.isEmpty())
                                        c->plotWidget()->clearOverlay();
                                    else
                                        c->plotWidget()->setOverlayText(text);
                                    return QVariant(true);
                                });

    // —— 展开数据列——
    // 表节点右键一键把整张表的数值列画进当前画布；上限 8 条（再多就成毛线团），
    // bool/文本列默认不画（bool 有专门的状态视图）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.expandColumns"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            PlotCanvas *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            const int dsId = params.value(QStringLiteral("dataset"), m_appCtx.activeDatasetId()).toInt();
            const QString tableName = params.value(QStringLiteral("table")).toString();
            const int limit = params.value(QStringLiteral("limit"), 8).toInt();
            if (dsId < 0 || tableName.isEmpty())
                return fail(err::Key, QStringLiteral("缺少 dataset / table"));
            const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(dsId);
            if (!ds)
                return fail(err::NoEnt, QStringLiteral("数据集不存在"));
            const nextpilot::plotscope::dataset::Table *t = ds->table(tableName);
            if (!t)
                return fail(err::NoEnt,
                            QStringLiteral("表不存在: %1").arg(tableName));

            QStringList numeric;
            for (const nextpilot::plotscope::dataset::Column &col : t->columns)
                if (col.type != nextpilot::plotscope::dataset::ValueType::Bool
                    && col.type != nextpilot::plotscope::dataset::ValueType::Text)
                    numeric << col.name;
            if (numeric.isEmpty())
                return fail(err::NoEnt,
                            QStringLiteral("表 %1 没有可画的数值列").arg(tableName));

            const int n = limit > 0 ? qMin(limit, int(numeric.size())) : int(numeric.size());
            for (int i = 0; i < n; ++i)
                if (activeChannelView())
                    activeChannelView()->addChannel(dsId, tableName, numeric.at(i),
                                                    nullptr,
                                                    QStringLiteral("%1.%2")
                                                        .arg(tableName, numeric.at(i)));
            QVariantMap out;
            out.insert(QStringLiteral("drawn"), n);
            out.insert(QStringLiteral("total"), int(numeric.size()));
            out.insert(QStringLiteral("truncated"), n < numeric.size());
            out.insert(QStringLiteral("names"), numeric.mid(0, n));
            return QVariant(out);
        });

    // —— 多源同名叠加/对比——
    //
    // 判定口径：同名 = **列名相同**（表可以不同；一个源里多表更常见）。
    // 曲线名统一「源名:table.column」，否则 N 条曲线挤在一起分不清谁是谁。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.overlaySame"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            PlotCanvas *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            int dsId = params.value(QStringLiteral("dataset"), m_appCtx.activeDatasetId()).toInt();
            const QString column = params.value(QStringLiteral("column"),
                                                m_appCtx.activeColumn()).toString();
            if (column.isEmpty() || dsId < 0)
                return fail(err::NoEnt, QStringLiteral("先在数据集树里选一个字段"));

            // 源名可能重复（同一份文件被加载两次、或同名不同路径），
            // 重名时补 #id —— 曲线名必须唯一，否则 N 条曲线在图上分不清。
            auto uniqSrc = [this](int srcId, const QString &name) -> QString {
                int dup = 0;
                for (int s : m_appCtx.sources().ids())
                {
                    const nextpilot::plotscope::dataset::DataSourceInfo *i =
                        m_appCtx.sources().get(s);
                    if (i && i->name == name)
                        ++dup;
                }
                return dup > 1 ? QStringLiteral("%1#%2").arg(name).arg(srcId) : name;
            };

            QVariantList addedNames;
            QString firstErr;
            int matched = 0;
            for (int srcId : m_appCtx.sources().ids())
            {
                const nextpilot::plotscope::dataset::DataSourceInfo *info =
                    m_appCtx.sources().get(srcId);
                if (!info)
                    continue;
                const QString srcName = uniqSrc(srcId,
                                                info->name.isEmpty()
                                                    ? QStringLiteral("src%1").arg(srcId)
                                                    : info->name);
                for (int d : info->datasetIds)
                {
                    const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(d);
                    if (!ds)
                        continue;
                    // 一个数据集里取第一张含该列的表（多表格式里同名通道通常只出现一次）。
                    QString hitTable;
                    for (const QString &tn : ds->tableNames())
                    {
                        const nextpilot::plotscope::dataset::Table *t = ds->table(tn);
                        if (!t)
                            continue;
                        for (const nextpilot::plotscope::dataset::Column &col : t->columns)
                            if (col.name == column)
                            {
                                hitTable = tn;
                                break;
                            }
                        if (!hitTable.isEmpty())
                            break;
                    }
                    if (hitTable.isEmpty())
                        continue;
                    ++matched;
                    const QString title =
                        QStringLiteral("%1:%2.%3").arg(srcName, hitTable, column);
                    QString addErr;
                    if (activeChannelView())
                    {
                        if (!activeChannelView()->addChannel(d, hitTable, column, &addErr, title))
                        {
                            // 取数失败不能静默：名字里有源和列，用户才追得住。
                            if (firstErr.isEmpty())
                                firstErr = QStringLiteral("%1.%2：%3")
                                               .arg(hitTable, column, addErr);
                            ++matched;
                            continue;
                        }
                    }
                    addedNames.append(title);
                }
            }
            if (addedNames.isEmpty())
                return fail(err::NoEnt,
                            firstErr.isEmpty()
                                ? QStringLiteral("没有其它源包含同名通道: %1").arg(column)
                                : firstErr);
            QVariantMap out;
            out.insert(QStringLiteral("matched"), matched);
            out.insert(QStringLiteral("added"), int(addedNames.size()));
            out.insert(QStringLiteral("names"), addedNames);
            return QVariant(out);
        });

    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.compareSame"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            PlotCanvas *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            const int dsId = params.value(QStringLiteral("dataset"), m_appCtx.activeDatasetId()).toInt();
            const QString column = params.value(QStringLiteral("column"),
                                                m_appCtx.activeColumn()).toString();
            if (column.isEmpty() || dsId < 0)
                return fail(err::NoEnt, QStringLiteral("先在数据集树里选一个字段"));

            // 只取两条：当前通道 + 第一个能匹配上的**别的源**。
            auto uniqSrc = [this](int srcId, const QString &name) -> QString {
                int dup = 0;
                for (int s : m_appCtx.sources().ids())
                {
                    const nextpilot::plotscope::dataset::DataSourceInfo *i =
                        m_appCtx.sources().get(s);
                    if (i && i->name == name)
                        ++dup;
                }
                return dup > 1 ? QStringLiteral("%1#%2").arg(name).arg(srcId) : name;
            };
            struct Hit { int datasetId; QString table; QString srcName; };
            Hit self{-1, QString(), QString()};
            Hit other{-1, QString(), QString()};
            bool hasOther = false;
            for (int srcId : m_appCtx.sources().ids())
            {
                const nextpilot::plotscope::dataset::DataSourceInfo *info =
                    m_appCtx.sources().get(srcId);
                if (!info)
                    continue;
                const QString srcName = uniqSrc(srcId,
                                                info->name.isEmpty()
                                                    ? QStringLiteral("src%1").arg(srcId)
                                                    : info->name);
                for (int d : info->datasetIds)
                {
                    const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(d);
                    if (!ds)
                        continue;
                    QString hitTable;
                    for (const QString &tn : ds->tableNames())
                    {
                        const nextpilot::plotscope::dataset::Table *t = ds->table(tn);
                        if (!t)
                            continue;
                        for (const nextpilot::plotscope::dataset::Column &col : t->columns)
                            if (col.name == column)
                            {
                                hitTable = tn;
                                break;
                            }
                        if (!hitTable.isEmpty())
                            break;
                    }
                    if (hitTable.isEmpty())
                        continue;
                    if (d == dsId)
                    {
                        self = Hit{d, hitTable, srcName};
                    }
                    else if (!hasOther && d != self.datasetId)
                    {
                        other = Hit{d, hitTable, srcName};
                        hasOther = true;
                        break; // 只取一条对照，够了
                    }
                }
                if (hasOther)
                    break;
            }
            if (self.datasetId < 0 || !hasOther)
                return fail(err::NoEnt,
                            QStringLiteral("没有别的源包含同名通道: %1").arg(column));

            const nextpilot::plotscope::dataset::Dataset *base = m_appCtx.data().get(self.datasetId);
            const nextpilot::plotscope::dataset::Dataset *peer = m_appCtx.data().get(other.datasetId);
            if (!base || !peer)
                return fail(err::NoEnt, QStringLiteral("数据集已失效"));

            // 对比前先清掉当前画布的曲线：AC-19 要求"恰两条"，残留会让条数不对。
            ChannelView *view = activeChannelView();
            if (!view)
                return fail(err::NoEnt, QStringLiteral("无活动画布视图"));
            view->clearChannels();
            QString e1, e2;
            const QString baseName = QStringLiteral("%1:%2.%3").arg(self.srcName, self.table, column);
            const QString peerName = QStringLiteral("%1:%2.%3").arg(other.srcName, other.table, column);
            if (!view->addChannel(self.datasetId, self.table, column, &e1, baseName))
                return fail(err::NoEnt, QStringLiteral("基准曲线没画上：%1").arg(e1));
            if (!view->addChannel(other.datasetId, other.table, column, &e2, peerName))
                return fail(err::NoEnt, QStringLiteral("对照曲线没画上：%1").arg(e2));

            // offset 建议与 T3 同一口径：**终点对齐**（baseEnd - peerEnd）。
            const qint64 offsetUs =
                qRound64((base->timeMax() - peer->timeMax()) * 1000000.0);
            QVariantMap out;
            out.insert(QStringLiteral("series"), 2);
            out.insert(QStringLiteral("offset_us"), QVariant::fromValue<qlonglong>(offsetUs));
            out.insert(QStringLiteral("base"), QStringLiteral("%1:%2.%3").arg(self.srcName, self.table, column));
            out.insert(QStringLiteral("peer"), QStringLiteral("%1:%2.%3").arg(other.srcName, other.table, column));
            out.insert(QStringLiteral("x_lo"), qMin(base->timeMin(), peer->timeMin()));
            out.insert(QStringLiteral("x_hi"), qMax(base->timeMax(), peer->timeMax()));
            return QVariant(out);
        });

    // —— page.*：页面/画布布局操作 ——
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
                                        return fail(err::Page, QStringLiteral("复制页失败"));
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
                                    // 上限 8×8（与 main 的选格一致）：也挡住脚本传 100×100
                                    // 造出上万个画布这种"参数合法但不合理"的输入。
                                    if (r < 1 || r > 8 || c < 1 || c > 8)
                                        return fail(QStringLiteral("egrid"), QStringLiteral("行/列须在 1~8 之间"));
                                    if (!setGrid(r, c))
                                        return fail(err::Grid, QStringLiteral("网格规格非法"));
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols}};
                                    return QVariant(out);
                                });
    // 老 buildLayoutPicker 的 m×n 选择器：点「网格」弹 8×8 悬停选格（抄 main LayoutPickGrid），
    // 选中即走 page.grid 重排；取消返回 false，不动当前布局。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.grid.pick"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    // 无窗口平台（ctest 的 offscreen）弹不出弹层：明确失败并指路
                                    // page.grid {rows,cols}，绝不阻塞事件循环（否则脚本/测试会挂死）。
                                    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
                                        return fail(QStringLiteral("egui"),
                                                    QStringLiteral("无窗口环境下无法选格，请用 page.grid {rows, cols}"));
                                    // 锚点取鼠标位置：点「网格」大按钮时光标正好在按钮上，
                                    // 等价于"贴按钮弹出"，不必让 MainWindow 传控件指针（少一层耦合）。
                                    const QPoint anchor = params.contains(QStringLiteral("anchor"))
                                        ? params.value(QStringLiteral("anchor")).toPoint()
                                        : QCursor::pos();
                                    int r = 0;
                                    int c = 0;
                                    if (!execGridPick(window(), anchor, m_rows, m_cols, &r, &c))
                                        return QVariant(false); // 用户取消
                                    if (!setGrid(r, c))
                                        return fail(err::Grid, QStringLiteral("网格规格非法"));
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
                                        return fail(err::Page, QStringLiteral("页签重命名参数非法"));
                                    m_pages.at(index)->title = title;
                                    m_tabs->setTabText(index, title);
                                    return QVariant(title);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.select"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int index = params.value(QStringLiteral("index")).toInt();
                                    if (index < 0 || index >= m_pages.size())
                                        return fail(err::Page, QStringLiteral("页索引越界"));
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
PS_REGISTER_PANEL("canvas-workspace", canvasWorkspace, createCanvasWorkspace)

} // namespace nextpilot::plotscope::gui

