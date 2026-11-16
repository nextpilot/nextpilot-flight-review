#include "PlottingSession.h"
#include "MainWindow.h"
#include "ModuleBridge.h"
#include "PlottingCanvas.h"
#include "PlottingLayout.h"
#include "PlottingTabs.h"
#include "ThemeManager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QMap>
#include <QMessageBox>
#include <QObject>
#include <QPair>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSize>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include "ps_kernel.h"
#include "filter/FilterCatalog.h"
#include "filter/FilterParamEditor.h"

using ps::plotting::PlottingLayout;
using ps::plotting::PlottingPage;

static QString plotModeLabel(int mode);

void PlottingSession::removeSourceFromCanvases(const QString &sourcePath)
{
    for (PlottingCanvas *c : pageCanvases())
        c->removeChannelsFromSource(sourcePath);
}

void PlottingSession::addChannels(const QVector<const PsChannelInfo *> &channels)
{
    for (PlottingCanvas *c : pageCanvases())
        for (const PsChannelInfo *ch : channels)
            c->addChannel(ch);
}

PlottingSession::PlottingSession(MainWindow *mw) : QWidget(mw), m_mw(mw)
{
    registerCommands();
}
PlottingSession::~PlottingSession() = default;
void PlottingSession::log(const QString &text) { m_mw->logs().info(text); }

void PlottingSession::createPages()
{
    m_plotWorkspace = new ps::plotting::PlottingTabs(
        [](QWidget *parent) { return new PlottingCanvas(parent); }, this);
    m_mw->setPageHost(m_plotWorkspace);
    m_plotWorkspace->createDefaultPages();
    m_plotWorkspace->setTimeMarkers(ps::bridge::timeMarkers(m_mw));

    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::canvasActivated, this,
            [this](PlottingCanvas *c) { refreshPropTree(c); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::canvasAddRequested, this,
            [this](PlottingCanvas *) { addCanvasToPage(); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::canvasSplitRequested, this,
            [this](PlottingCanvas *c, int orientation) {
                splitCanvas(c, orientation == 0 ? Qt::Horizontal : Qt::Vertical);
            });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::addChannelRequested, this,
            [this](PlottingCanvas *c) {
                if (ps::bridge::sourceChannels(m_mw).isEmpty())
                {
                    log(QStringLiteral("先加载数据文件，再添加曲线"));
                    return;
                }
                QStringList names;
                for (const PsChannelInfo *ch : ps::bridge::sourceChannels(m_mw))
                {
                    const QString n = QString::fromUtf8(ch->name);
                    names.append(ch->unit ? QStringLiteral("%1 [%2]").arg(n, QString::fromUtf8(ch->unit)) : n);
                }
                bool ok = false;
                const QString pick = QInputDialog::getItem(m_mw, QStringLiteral("添加曲线"),
                                                           QStringLiteral("通道"), names, 0, false, &ok);
                if (ok && !pick.isEmpty())
                {
                    c->addChannel(ps::bridge::sourceChannels(m_mw).value(names.indexOf(pick)));
                    log(QStringLiteral("添加曲线：%1").arg(pick));
                }
            });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::canvasMaximizeRequested, this,
            [this](PlottingCanvas *c) { toggleCanvasMaximize(c); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::seriesSelected, this,
            [this](PlottingCanvas *c, int index) { showSeriesProps(c, index); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::markerRequested, this,
            [this](double t) { ps::bridge::addTimeMarker(m_mw, t, QString(), true); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::markerRemoveRequested, this,
            [this](const QString &id) { ps::bridge::removeTimeMarker(m_mw, id); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::markerMoveRequested, this,
            [this](const QString &id, double to) { ps::bridge::moveTimeMarker(m_mw, id, to); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::markerRenameRequested, this,
            [this](const QString &id, const QString &label) { ps::bridge::renameTimeMarker(m_mw, id, label); });
    connect(m_plotWorkspace, &ps::plotting::PlottingTabs::canvasRemoveRequested, this,
            [this](PlottingCanvas *c) { removeCanvas(c); });

    connect(m_mw->pages(), &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (m_mw->pages()->count() > 2 && !m_mw->pages()->tabText(idx).isEmpty())
            m_mw->pages()->removeTab(idx);
    });

    connect(m_mw->pages(), &QTabWidget::currentChanged, this, [this](int i) {
        if (m_maxedCanvas)
            restoreCanvasMaximize();
        refreshPropTree(currentCanvas());
        if (i >= 0 && i == m_mw->pages()->count() - 1 && m_mw->pages()->tabText(i) == QStringLiteral("＋"))
            newPage();
    });
}

void PlottingSession::applyCanvasGrid(int rows, int cols)
{
    QWidget *page = m_mw->pages() ? m_mw->pages()->currentWidget() : nullptr;
    if (!page || dynamic_cast<QLabel *>(page))
    {
        log(QStringLiteral("当前页不是绘图页"));
        return;
    }
    if (PlottingLayout::collectCanvases(page).isEmpty())
    {
        log(QStringLiteral("当前页没有画布"));
        return;
    }

    auto *layout = PlottingLayout::layoutOf(page);
    if (!layout)
    {
        // 兼容旧的单画布/裸 QSplitter 页，先统一成可滚动的页容器。
        const int idx = m_mw->pages()->currentIndex();
        const QString tabText = m_mw->pages()->tabText(idx);
        m_mw->pages()->removeTab(idx);
        auto *host = new PlottingPage(m_mw->pages());
        layout = host->layout();
        layout->addWidget(page);
        m_mw->pages()->insertTab(idx, host, tabText);
        m_mw->pages()->setCurrentIndex(idx);
    }
    // m×n：先删空白画布，再按「列数固定、行数只增不减到 m」补格；
    // 空出来的格子补真空白画布，谁都能往里拖通道。
    QVector<PlottingCanvas *> all = layout->canvases();
    QVector<PlottingCanvas *> keep;
    for (PlottingCanvas *c : all)
        if (canvasHasData(c))
            keep.append(c);
    if (keep.isEmpty())
        keep.append(all.first());  // 整页都没数据时留一个，别把页清空

    for (PlottingCanvas *c : all)
    {
        if (keep.contains(c))
            continue;
        if (m_maxedCanvas == c)
            restoreCanvasMaximize();
        c->setParent(nullptr);
        c->deleteLater();
    }

    const int c = qBound(1, cols, 8);
    const int minRows = qBound(1, rows, 8);
    const int totalRows = qMax(minRows, (keep.size() + c - 1) / c);
    const int cells = totalRows * c;
    while (keep.size() < cells)
        keep.append(makeBlankCanvas());

    layout->setCanvases(keep);
    layout->setGrid(minRows, c);
    log(QStringLiteral("页内布局：%1 × %2（画布 %3）").arg(totalRows).arg(c).arg(keep.size()));
}

void PlottingSession::toggleCanvasMaximize(PlottingCanvas *c)
{
    if (m_maxedCanvas == c)
    {
        restoreCanvasMaximize();
        return;
    }
    if (m_maxedCanvas)
        restoreCanvasMaximize();
    if (!c || !m_mw->pages() || !m_mw->pages()->isAncestorOf(c))
        return;
    m_sizeBeforeMax = size();

    QWidget *page = pageForCanvas(c);
    if (!page)
        return;

    // 逐级隐藏同级兄弟：画布填满当前页，不脱离原有 splitter 树。
    QWidget *child = c;
    while (child && child != page)
    {
        QSplitter *sp = qobject_cast<QSplitter *>(child->parentWidget());
        if (!sp)
            break;
        m_maxedSizes.append({sp, sp->sizes()});
        for (int i = 0; i < sp->count(); ++i)
        {
            QWidget *w = sp->widget(i);
            if (w && w != child && !w->isHidden())
            {
                w->hide();
                m_maxedHidden.append(w);
            }
        }
        child = sp;
    }
    m_maxedCanvas = c;
    c->setMaximizedState(true);
    log(QStringLiteral("最大化画布：%1").arg(c->title()));
}

void PlottingSession::splitCanvas(PlottingCanvas *target, Qt::Orientation orientation)
{
    QWidget *page = m_mw->pages() ? m_mw->pages()->currentWidget() : nullptr;
    if (!page || !target || dynamic_cast<QLabel *>(page))
        return;

    auto *layout = PlottingLayout::layoutOf(page);
    if (!layout)
    {
        // 兼容旧页：把原来的画布/splitter 整棵挂到一个 PlottingLayout 下，结构不丢。
        const int idx = m_mw->pages()->currentIndex();
        const QString tabText = m_mw->pages()->tabText(idx);
        m_mw->pages()->removeTab(idx);
        auto *host = new PlottingPage(m_mw->pages());
        layout = host->layout();
        layout->addWidget(page);
        m_mw->pages()->insertTab(idx, host, tabText);
        m_mw->pages()->setCurrentIndex(idx);
        page = host;
    }

    auto *c = new PlottingCanvas(m_mw->pages());
    c->clearChannels();
    c->setTitle(QStringLiteral("新画布 · plot"));
    c->setTimeRange(target->t0(), target->t1());
    m_plotWorkspace->attachCanvas(c);
    if (layout->isGridMode())
    {
        // 网格里拆分：只把目标格子包成嵌套 splitter，其他格子尺寸不动
        QSplitter *row = qobject_cast<QSplitter *>(target->parentWidget());
        if (!row)
            return;
        const int at = row->indexOf(target);
        QList<int> sizes = row->sizes();
        auto *nested = new PlottingLayout(orientation, row);
        row->insertWidget(at, nested);
        nested->addCanvas(target);
        nested->addCanvas(c);
        if (!sizes.isEmpty())
        {
            // 其他格子尺寸原样保留，只有被拆分这一格内部对半分
            const int cell = sizes.value(at, 1);
            row->setSizes(sizes);
            QTimer::singleShot(0, nested, [nested, cell] {
                nested->setSizes({cell / 2, cell / 2});
            });
        }
        layout->clearGridMode();
        log(QStringLiteral("网格内拆分：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                            : QStringLiteral("上下")));
        refreshPropTree(c);
        return;
    }
    // 手动合并/拆分就不再保持 m×n 整格，之后添加删除只动相关那一格
    layout->clearGridMode();
    layout->split(target, orientation, c);
    log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                          : QStringLiteral("上下")));
    refreshPropTree(c);
}

void PlottingSession::createPlotPage(const QString &kind)
{
    struct PlotKind
    {
        const char *kind;
        const char *label;
        PlottingCanvas::PlotMode mode;
    };
    static const PlotKind kinds[] = {
        {"timeseries", "时间序列", PlottingCanvas::TimeSeries},
        {"xy", "XY 曲线", PlottingCanvas::Xy},
        {"scatter", "散点图", PlottingCanvas::Scatter},
        {"bar", "柱状图", PlottingCanvas::BarChart},
        {"histogram", "直方图", PlottingCanvas::Histogram},
        {"fft", "频谱 FFT", PlottingCanvas::Spectrum},
        {"traj3d", "3D 轨迹", PlottingCanvas::Traj3D},
        {"map", "GPS 地图", PlottingCanvas::GpsMap},
        {"surface3d", "3D 曲面", PlottingCanvas::Surface3D},
        {"horizon", "地平仪", PlottingCanvas::Horizon},
        {"compass", "罗盘", PlottingCanvas::Compass},
        {"gauge", "表盘", PlottingCanvas::Gauge},
    };
    const PlotKind *spec = nullptr;
    for (const PlotKind &k : kinds)
        if (kind == QLatin1String(k.kind))
            spec = &k;
    if (!spec)
    {
        log(QStringLiteral("未知图型：%1").arg(kind));
        return;
    }
    const QString label = QString::fromUtf8(spec->label);
    if (spec->mode == PlottingCanvas::TimeSeries)
    {
        newPage();
        if (auto *c = currentCanvas())
        {
            c->setTitle(QStringLiteral("%1 · 选择通道").arg(label));
            log(QStringLiteral("新建图页：%1").arg(label));
        }
        return;
    }

    auto *host = new PlottingPage(m_mw->pages());
    auto *layout = host->layout();
    auto *c = new PlottingCanvas(layout);
    c->clearChannels();
    c->setTitle(label);
    c->setPlotMode(spec->mode, kind);
    layout->addCanvas(c);
    PlottingCanvas *seed = nullptr;
    int best = 0;
    for (PlottingCanvas *w : findChildren<PlottingCanvas *>())
    {
        if (w == c)
            continue;
        const int n = w->sampleSeries().size();
        if (n > best)
        {
            best = n;
            seed = w;
        }
    }
    if (seed)
        for (const PlottingCanvas::SampleSeries &s : seed->sampleSeries())
            c->addGeneratedSeries(s.name, s.time, s.value);
    m_plotWorkspace->attachCanvas(c);
    const int plusIdx = m_mw->pages()->count() - 1;
    m_mw->pages()->insertTab(plusIdx, host, label);
    m_mw->pages()->setCurrentWidget(host);
    log(QStringLiteral("新建图页：%1").arg(label));
}

PlottingCanvas *PlottingSession::makeBlankCanvas()
{
    auto *c = new PlottingCanvas(m_mw->pages());
    c->clearChannels();
    c->setTitle(QStringLiteral("空白画布"));
    m_plotWorkspace->attachCanvas(c);
    return c;
}

bool PlottingSession::canvasHasData(PlottingCanvas *c)
{
    return c && c->saveState().value(QStringLiteral("series")).toArray().size() > 0;
}

void PlottingSession::addCanvasToPage()
{
    QWidget *page = m_mw->pages()->currentWidget();
    if (qobject_cast<QLabel *>(page))
    {
        log(QStringLiteral("该页不是绘图页，先切到绘图页再加画布"));
        return;
    }

    PlottingCanvas *target = currentCanvas();
    if (!target)
        target = pageCanvases().value(0, nullptr);
    if (!target)
    {
        log(QStringLiteral("当前页没有可拆分的画布"));
        return;
    }

    // 添加：整格网格里追加一格；已经手动合并拆分过的页只拆当前画布，不动其他画布
    auto *layout = PlottingLayout::layoutOf(page);
    if (layout && !layout->isGridMode())
    {
        splitCanvas(target, Qt::Vertical);
        return;
    }
    if (layout)
    {
        auto *c = new PlottingCanvas(m_mw->pages());
        c->clearChannels();
        c->setTitle(QStringLiteral("新画布 · plot"));
        c->setTimeRange(target->t0(), target->t1());
        m_plotWorkspace->attachCanvas(c);

        QVector<PlottingCanvas *> ordered = layout->canvases();
        ordered.append(c);
        const int cols = layout->gridCols();
        const int minRows = layout->minRows();
        const int totalRows = qMax(minRows, (ordered.size() + cols - 1) / cols);
        while (ordered.size() < totalRows * cols)
            ordered.append(makeBlankCanvas());
        layout->setCanvases(ordered);
        layout->setGrid(minRows, cols);
        refreshPropTree(c);
        log(QStringLiteral("添加画布"));
        return;
    }
}

void PlottingSession::removeCanvas(PlottingCanvas *c)
{
    if (m_maxedCanvas == c)
        restoreCanvasMaximize();
    if (!c)
        return;

    QWidget *page = pageForCanvas(c);
    if (!page)
        return;
    // 判据是「整页剩余画布 ≥1」，不是「所在 splitter 还有别的子项」：
    // 网格布局最后一行只有一个画布时，它的父行 splitter 只有它，但它仍然该能删
    if (PlottingLayout::collectCanvases(page).size() <= 1)
    {
        log(QStringLiteral("页内至少保留一个画布"));
        return;
    }

    auto *layout = PlottingLayout::layoutOf(page);
    if (layout && layout->isGridMode())
    {
        // 网格模式：原位补一块空白画布，其他画布位置不动；
        // 只有末尾整行都空着才收行，且行数不低于设定值。
        const int cols = layout->gridCols();
        const int minRows = layout->minRows();
        QVector<PlottingCanvas *> ordered = layout->canvases();
        const int at = ordered.indexOf(c);
        if (at < 0)
            return;
        ordered.removeAt(at);
        ordered.insert(qMin(at, ordered.size()), makeBlankCanvas());

        while (ordered.size() > minRows * cols && ordered.size() % cols == 0)
        {
            const int lastRow = ordered.size() - cols;
            bool allBlank = true;
            for (int i = lastRow; i < ordered.size(); ++i)
                if (canvasHasData(ordered[i]))
                    allBlank = false;
            if (!allBlank)
                break;
            for (int i = 0; i < cols; ++i)
                ordered.takeLast()->deleteLater();
        }
        layout->setCanvases(ordered);
        layout->setGrid(minRows, cols);
        c->deleteLater();
        refreshPropTree(currentCanvas());
        log(QStringLiteral("删除画布"));
        return;
    }

    QWidget *holder = c->parentWidget();
    if (layout)
    {
        // splitter 模式：其余画布尺寸原样保留，删掉的空间并入相邻一格
        if (auto *sp = qobject_cast<QSplitter *>(holder))
        {
            const int at = sp->indexOf(c);
            QList<int> sizes = sp->sizes();
            layout->removeCanvas(c);
            if (at >= 0)
            {
                const int freed = sizes.value(at, 0);
                sizes.removeAt(at);
                if (!sizes.isEmpty())
                {
                    const int mergeTo = qBound(0, at > 0 ? at - 1 : 0, sizes.size() - 1);
                    sizes[mergeTo] += freed;
                    sp->setSizes(sizes);
                }
            }
        }
        else
        {
            layout->removeCanvas(c);
        }
    }
    else if (qobject_cast<QSplitter *>(holder))
    {
        // deleteLater 之前必须先脱开父级，QSplitter::count() 才会立刻反映删除
        c->setParent(nullptr);
    }
    else
    {
        log(QStringLiteral("该画布不在可编辑的布局内"));
        return;
    }

    // 自下而上收掉变空的 splitter，合并回相邻画布；其他分支的尺寸不动
    while (holder && holder != page)
    {
        QSplitter *sp = qobject_cast<QSplitter *>(holder);
        if (!sp || sp->count() > 0)
            break;
        QWidget *parentOfHolder = sp->parentWidget();
        sp->setParent(nullptr);
        sp->deleteLater();
        holder = parentOfHolder;
    }

    c->deleteLater();
    log(QStringLiteral("删除画布"));
    refreshPropTree(currentCanvas());
}

QWidget *PlottingSession::buildPage(const QJsonObject &o) { return buildNode(o, true); }

QWidget *PlottingSession::buildNode(const QJsonObject &o, bool top)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas"))
    {
        auto *c = new PlottingCanvas(m_mw->pages());
        c->clearChannels();
        QHash<QString, const PsChannelInfo *> pool;
        for (const ps::bridge::SourceView &src : ps::bridge::sourceViews(m_mw))
        {
            for (const PsChannelInfo *ch : src.channels)
            {
                const QString name = QString::fromUtf8(ch->name);
                pool.insert(src.path + QLatin1Char('|') + name, ch);
                if (!pool.contains(name))
                    pool.insert(name, ch);
            }
        }
        c->restoreState(o, pool);
        m_plotWorkspace->attachCanvas(c);
        return c;
    }
    if (type == QLatin1String("grid"))
    {
        QVector<PlottingCanvas *> canvases;
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            if (auto *c = qobject_cast<PlottingCanvas *>(buildNode(v.toObject(), false)))
                canvases.append(c);
        auto *host = top ? new PlottingPage(m_mw->pages()) : nullptr;
        auto *layout = host ? host->layout() : new PlottingLayout(Qt::Vertical, m_mw->pages());
        for (PlottingCanvas *c : canvases)
            layout->addCanvas(c);
        layout->setGrid(o.value(QStringLiteral("rows")).toInt(1), o.value(QStringLiteral("cols")).toInt(1));
        return host ? static_cast<QWidget *>(host) : static_cast<QWidget *>(layout);
    }
    if (type == QLatin1String("splitter") || type == QLatin1String("layout"))
    {
        const Qt::Orientation ori =
            o.value(QStringLiteral("vertical")).toBool(true) ? Qt::Vertical : Qt::Horizontal;
        auto *host = (top && ori == Qt::Vertical) ? new PlottingPage(m_mw->pages()) : nullptr;
        auto *layout = host ? host->layout() : new PlottingLayout(ori, m_mw->pages());
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            layout->addWidget(buildNode(v.toObject(), false));
        QList<int> sizes;
        for (const QJsonValue &v : o.value(QStringLiteral("sizes")).toArray())
            sizes.append(v.toInt());
        if (!sizes.isEmpty())
            layout->setSizes(sizes);
        return host ? static_cast<QWidget *>(host) : static_cast<QWidget *>(layout);
    }
    return new QLabel(o.value(QStringLiteral("text")).toString(), m_mw->pages());
}

void PlottingSession::refreshPropTree(PlottingCanvas *active)
{
    if (!m_propTree)
        return;
    QWidget *page = active ? pageForCanvas(active)
                           : (m_mw->pages() ? m_mw->pages()->currentWidget() : nullptr);
    if (!page || !m_mw->pages() || page == m_mw->pages()->widget(m_mw->pages()->count() - 1))
        return;

    m_propTreeBuilding = true;
    m_propTree->clear();

    const QString pageTitle = m_mw->pages()->tabText(m_mw->pages()->indexOf(page));
    auto *root = new QTreeWidgetItem(m_propTree);
    root->setText(0, pageTitle);
    root->setIcon(0, ThemeManager::icon(QStringLiteral(":/icons/panel.svg")));
    root->setFlags(root->flags() & ~Qt::ItemIsSelectable);
    root->setExpanded(true);

    QVector<PlottingCanvas *> canvases = PlottingLayout::collectCanvases(page);
    QTreeWidgetItem *selected = nullptr;
    for (PlottingCanvas *c : canvases)
    {
        auto *axes = new QTreeWidgetItem(root);
        axes->setText(0, c->title());
        axes->setIcon(0, ThemeManager::icon(QStringLiteral(":/icons/prop.svg")));
        axes->setData(0, Qt::UserRole, 1);
        axes->setData(0, Qt::UserRole + 1, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(c)));
        axes->setExpanded(active ? (c == active) : (c == canvases.first()));

        const QJsonObject st = c->saveState();
        const QJsonArray arr = st.value(QStringLiteral("series")).toArray();
        for (int i = 0; i < arr.size(); ++i)
        {
            const QJsonObject so = arr.at(i).toObject();
            const QString label = so.value(QStringLiteral("label")).toString(
                so.value(QStringLiteral("name")).toString());
            const int axis = so.value(QStringLiteral("axis")).toInt(0);
            auto *line = new QTreeWidgetItem(axes);
            line->setText(0, QStringLiteral("%1%2").arg(label, axis == 1 ? QStringLiteral("  → 右轴") : QString()));
            line->setData(0, Qt::UserRole, 2);
            line->setData(0, Qt::UserRole + 1, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(c)));
            line->setData(0, Qt::UserRole + 2, i);
            QPixmap sw(10, 10);
            sw.fill(QColor(so.value(QStringLiteral("color")).toString()));
            line->setIcon(0, QIcon(sw));
        }
        if (c == active || (!active && !selected))
            selected = axes;
    }
    m_propTreeBuilding = false;

    if (selected)
    {
        m_propTree->setCurrentItem(selected);
        PlottingCanvas *c = reinterpret_cast<PlottingCanvas *>(
            selected->data(0, Qt::UserRole + 1).value<quintptr>());
        if (c)
            showAxesProps(c);
    }
}


void PlottingSession::applyTimeMarkers()
{
    if (m_plotWorkspace)
        m_plotWorkspace->setTimeMarkers(ps::bridge::timeMarkers(m_mw));
}


void PlottingSession::newPage()
{
    auto *host = new PlottingPage(m_mw->pages());
    auto *layout = host->layout();
    auto *c = new PlottingCanvas(layout);
    c->clearChannels();
    c->setTitle(QStringLiteral("新页 · 画布"));
    layout->addCanvas(c);
    m_plotWorkspace->attachCanvas(c);
    const int plusIdx = m_mw->pages()->count() - 1;
    m_mw->pages()->insertTab(plusIdx, host, QStringLiteral("新页"));
    m_mw->pages()->setCurrentWidget(host);
    refreshPropTree(c);
    log(QStringLiteral("新建页"));
}

void PlottingSession::restoreCanvasMaximize()
{
    if (!m_maxedCanvas)
        return;
    PlottingCanvas *c = m_maxedCanvas;
    m_maxedCanvas = nullptr;
    for (QWidget *w : m_maxedHidden)
        if (w)
            w->show();
    for (const auto &p : m_maxedSizes)
        if (p.first)
            p.first->setSizes(p.second);
    m_maxedHidden.clear();
    m_maxedSizes.clear();
    c->setMaximizedState(false);
    // 隐藏/显示画布会触发 QMainWindow 重算 sizeHint，恢复时把窗口尺寸拉回去。
    const QSize sz = m_sizeBeforeMax;
    QTimer::singleShot(0, this, [this, sz] { if (sz.isValid()) resize(sz); });
}





QWidget *PlottingSession::pageForCanvas(PlottingCanvas *c) const
{
    if (!m_mw->pages() || !c)
        return nullptr;
    for (int i = 0; i < m_mw->pages()->count(); ++i)
    {
        QWidget *w = m_mw->pages()->widget(i);
        if (w && (w == c || w->isAncestorOf(c)))
            return w;
    }
    return nullptr;
}

QVector<PlottingCanvas *> PlottingSession::pageCanvases() const
{
    return PlottingLayout::collectCanvases(m_mw->pages() ? m_mw->pages()->currentWidget() : nullptr);
}

PlottingCanvas *PlottingSession::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}
















void PlottingSession::applyPlayhead(double t)
{
    for (PlottingCanvas *c : findChildren<PlottingCanvas *>())
    {
        c->setPlayhead(t);
        if (c->plotMode() == PlottingCanvas::TimeSeries)
        {
            const double span = std::max(0.5, c->t1() - c->t0());
            if (t < c->t0() || t > c->t1())
                c->setTimeRange(t - span * 0.8, t + span * 0.2);
        }
    }
}

class SeriesEditDialog : public QDialog
{
public:
    SeriesEditDialog(const QJsonObject &st, const QVector<const PsChannelInfo *> &channels,
                     const QString &alias, QWidget *parent = nullptr)
        : QDialog(parent), m_channels(channels), m_alias(alias)
    {
        setWindowTitle(QStringLiteral("曲线属性"));
        resize(430, 340);
        m_color = QColor(st.value(QStringLiteral("color")).toString());
        if (!m_color.isValid())
            m_color = ThemeManager::seriesColor(0);

        auto *root = new QVBoxLayout(this);
        auto *tabs = new QTabWidget(this);

        // 曲线：线型 / 颜色 / 线宽 / 轴 / 可见
        auto *curvePage = new QWidget(tabs);
        auto *curveForm = new QFormLayout(curvePage);
        m_label = new QLineEdit(st.value(QStringLiteral("label")).toString(), curvePage);
        curveForm->addRow(QStringLiteral("图例"), m_label);
        m_colorBtn = new QPushButton(curvePage);
        m_colorBtn->setFixedHeight(24);
        setColorButton();
        connect(m_colorBtn, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_color, this, QStringLiteral("曲线颜色"));
            if (c.isValid())
            {
                m_color = c;
                setColorButton();
            }
        });
        curveForm->addRow(QStringLiteral("颜色"), m_colorBtn);
        m_width = new QSpinBox(curvePage);
        m_width->setRange(1, 6);
        m_width->setSuffix(QStringLiteral(" px"));
        m_width->setValue(st.value(QStringLiteral("width")).toInt(1));
        curveForm->addRow(QStringLiteral("线宽"), m_width);
        m_style = new QComboBox(curvePage);
        m_style->addItem(QStringLiteral("实线"), 0);
        m_style->addItem(QStringLiteral("虚线"), 1);
        m_style->addItem(QStringLiteral("点线"), 2);
        m_style->addItem(QStringLiteral("点划线"), 3);
        m_style->setCurrentIndex(m_style->findData(st.value(QStringLiteral("style")).toInt(0)));
        curveForm->addRow(QStringLiteral("线型"), m_style);
        m_marker = new QComboBox(curvePage);
        m_marker->addItem(QStringLiteral("无"), 0);
        m_marker->addItem(QStringLiteral("圆点"), 1);
        m_marker->addItem(QStringLiteral("方块"), 2);
        m_marker->addItem(QStringLiteral("菱形"), 3);
        m_marker->addItem(QStringLiteral("三角"), 4);
        m_marker->addItem(QStringLiteral("十字"), 5);
        m_marker->addItem(QStringLiteral("叉号"), 6);
        m_marker->setCurrentIndex(m_marker->findData(st.value(QStringLiteral("marker")).toInt(0)));
        curveForm->addRow(QStringLiteral("标记"), m_marker);
        m_markerSize = new QSpinBox(curvePage);
        m_markerSize->setRange(1, 12);
        m_markerSize->setSuffix(QStringLiteral(" px"));
        m_markerSize->setValue(st.value(QStringLiteral("markerSize")).toInt(5));
        curveForm->addRow(QStringLiteral("标记大小"), m_markerSize);
        m_axis = new QComboBox(curvePage);
        m_axis->addItem(QStringLiteral("左 Y 轴"), 0);
        m_axis->addItem(QStringLiteral("右 Y 轴"), 1);
        m_axis->setCurrentIndex(st.value(QStringLiteral("axis")).toInt(0) == 1 ? 1 : 0);
        curveForm->addRow(QStringLiteral("绑定轴"), m_axis);
        m_visible = new QCheckBox(QStringLiteral("显示该曲线"), curvePage);
        m_visible->setChecked(st.value(QStringLiteral("visible")).toBool(true));
        curveForm->addRow(QStringLiteral("可见"), m_visible);
        tabs->addTab(curvePage, QStringLiteral("曲线"));

        // X 轴：时间偏移
        auto *xPage = new QWidget(tabs);
        auto *xForm = new QFormLayout(xPage);
        m_offset = new QDoubleSpinBox(xPage);
        m_offset->setRange(-1e6, 1e6);
        m_offset->setDecimals(6);
        m_offset->setSuffix(QStringLiteral(" s"));
        m_offset->setValue(st.value(QStringLiteral("offset")).toDouble(0));
        xForm->addRow(QStringLiteral("时间偏移"), m_offset);
        auto *xNote = new QLabel(QStringLiteral("显示时间 = 原始时间 − 偏移；只改显示，不改原始数据。"), xPage);
        xNote->setWordWrap(true);
        xNote->setStyleSheet(QStringLiteral("color:#8494a0;"));
        xForm->addRow(xNote);
        tabs->addTab(xPage, QStringLiteral("X 轴"));

        // Y 轴：换数据列 / 算子 / 脚本
        auto *yPage = new QWidget(tabs);
        auto *yForm = new QFormLayout(yPage);
        m_column = new QComboBox(yPage);
        const QString curName = st.value(QStringLiteral("name")).toString();
        for (const PsChannelInfo *ch : m_channels)
            if (ch)
                m_column->addItem(QString::fromUtf8(ch->name),
                                  QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
        const int ci = m_column->findText(curName);
        if (ci >= 0)
            m_column->setCurrentIndex(ci);
        yForm->addRow(QStringLiteral("数据列"), m_column);
        m_transform = new QComboBox(yPage);
        m_transform->addItem(QStringLiteral("原值（无变换）"), QString());
        std::size_t filterCount = 0;
        const ps::filter::Def *defs = ps::filter::catalog(&filterCount);
        for (std::size_t i = 0; i < filterCount; ++i)
        {
            if (QString::fromUtf8(defs[i].id) == QLatin1String("none"))
                continue;
            m_transform->addItem(QString::fromUtf8(defs[i].label), QString::fromUtf8(defs[i].id));
        }
        m_transform->addItem(QStringLiteral("Lua 脚本"), QStringLiteral("lua"));
        yForm->addRow(QStringLiteral("变换"), m_transform);

        m_transformStack = new QStackedWidget(yPage);
        auto *nonePage = new QWidget(m_transformStack);
        auto *noneForm = new QFormLayout(nonePage);
        auto *noneTip = new QLabel(QStringLiteral("原值不应用算子；选择算子后在这里设置参数。"), nonePage);
        noneTip->setWordWrap(true);
        noneTip->setStyleSheet(QStringLiteral("color:#8494a0;"));
        noneForm->addRow(noneTip);
        m_transformStack->addWidget(nonePage);

        m_paramEditor = new FilterParamEditor(m_transformStack);
        m_paramEditor->setFilter(QStringLiteral("moving_average"));
        m_transformStack->addWidget(m_paramEditor);

        auto *scriptPage = new QWidget(m_transformStack);
        auto *scriptForm = new QFormLayout(scriptPage);
        scriptForm->setContentsMargins(0, 0, 0, 0);
        m_script = new QPlainTextEdit(scriptPage);
        m_script->setPlaceholderText(QStringLiteral("out = ps.movingAvg(ps.input(1), 5)"));
        m_script->setPlainText(QStringLiteral("out = ps.movingAvg(ps.input(1), 5)"));
        m_script->setFixedHeight(78);
        scriptForm->addRow(QStringLiteral("脚本"), m_script);
        m_transformStack->addWidget(scriptPage);

        yForm->addRow(QStringLiteral("参数 / 脚本"), m_transformStack);
        auto *yNote = new QLabel(QStringLiteral("算子/脚本会把结果写成派生曲线；脚本输入为 ps.input(1)，输出到 out。"), yPage);
        yNote->setWordWrap(true);
        yNote->setStyleSheet(QStringLiteral("color:#8494a0;"));
        yForm->addRow(yNote);
        connect(m_transform, &QComboBox::currentIndexChanged, this, [this] {
            const QString id = m_transform->currentData().toString();
            if (id == QLatin1String("lua"))
            {
                m_transformStack->setCurrentIndex(2);
            }
            else if (id.isEmpty())
            {
                m_transformStack->setCurrentIndex(0);
            }
            else
            {
                m_paramEditor->setFilter(id);
                m_transformStack->setCurrentIndex(1);
            }
        });
        m_transformStack->setCurrentIndex(0);
        tabs->addTab(yPage, QStringLiteral("Y 轴"));

        root->addWidget(tabs);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
        bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(bb);
    }

    QJsonObject patch() const
    {
        QJsonObject o;
        o[QStringLiteral("label")] = m_label->text().trimmed();
        o[QStringLiteral("color")] = m_color.name();
        o[QStringLiteral("width")] = m_width->value();
        o[QStringLiteral("style")] = m_style->currentData().toInt();
        o[QStringLiteral("marker")] = m_marker->currentData().toInt();
        o[QStringLiteral("markerSize")] = m_markerSize->value();
        o[QStringLiteral("axis")] = m_axis->currentData().toInt();
        o[QStringLiteral("visible")] = m_visible->isChecked();
        o[QStringLiteral("offset")] = m_offset->value();
        return o;
    }
    QString transformId() const { return m_transform->currentData().toString(); }
    QString transformLabel() const
    {
        const QString id = transformId();
        if (id.isEmpty())
            return QStringLiteral("原值");
        if (id == QLatin1String("lua"))
            return QStringLiteral("脚本");
        const ps::filter::Def *def = ps::filter::find(id.toUtf8().constData());
        return def ? QString::fromUtf8(def->short_label) : m_transform->currentText();
    }
    QJsonObject transformParams() const { return m_paramEditor->params(); }
    QString scriptText() const { return m_script->toPlainText(); }
    const PsChannelInfo *channel() const
    {
        const QVariant v = m_column->currentData();
        return v.isValid() ? reinterpret_cast<const PsChannelInfo *>(v.toULongLong()) : nullptr;
    }
    QString channelName() const { return m_column->currentText(); }
    QString alias() const { return m_alias; }

private:
    void setColorButton()
    {
        m_colorBtn->setText(m_color.name());
        m_colorBtn->setStyleSheet(ThemeManager::mapStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:#0d1117;border:1px solid #3a4854;border-radius:2px;"
            "font-weight:600;}QPushButton:hover{border-color:#f2b134;}").arg(m_color.name())));
    }

    QVector<const PsChannelInfo *> m_channels;
    QString m_alias;
    QColor m_color;
    QLineEdit *m_label = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QSpinBox *m_width = nullptr;
    QComboBox *m_style = nullptr;
    QComboBox *m_marker = nullptr;
    QSpinBox *m_markerSize = nullptr;
    QComboBox *m_axis = nullptr;
    QCheckBox *m_visible = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QComboBox *m_column = nullptr;
    QComboBox *m_transform = nullptr;
    QStackedWidget *m_transformStack = nullptr;
    FilterParamEditor *m_paramEditor = nullptr;
    QPlainTextEdit *m_script = nullptr;
};

void PlottingSession::showAxesProps(PlottingCanvas *c)
{
    if (!c)
        return;
    const QJsonObject st = c->saveState();
    const QJsonArray arr = st.value(QStringLiteral("series")).toArray();
    int right = 0;
    for (const QJsonValue &v : arr)
        if (v.toObject().value(QStringLiteral("axis")).toInt(0) == 1)
            ++right;
    const bool autoY = st.value(QStringLiteral("autoY")).toBool(true);
    const QString y1 = autoY ? QStringLiteral("自动量程")
                             : QStringLiteral("%1 ~ %2").arg(st.value(QStringLiteral("yMin")).toDouble(), 0, 'f', 3)
                                                       .arg(st.value(QStringLiteral("yMax")).toDouble(), 0, 'f', 3);
    const QString y2 = right > 0
        ? QStringLiteral("%1 ~ %2").arg(st.value(QStringLiteral("y2Min")).toDouble(), 0, 'f', 3)
                                    .arg(st.value(QStringLiteral("y2Max")).toDouble(), 0, 'f', 3)
        : QStringLiteral("未启用");
    setPropRows(QStringLiteral("坐标轴 · %1").arg(c->title()),
                {{QStringLiteral("类型"), QStringLiteral("坐标轴 (Axes)")},
                 {QStringLiteral("标题"), c->title()},
                 {QStringLiteral("绘图模式"), plotModeLabel(st.value(QStringLiteral("mode")).toInt())},
                 {QStringLiteral("时间轴"), QStringLiteral("%1 ~ %2 s")
                                               .arg(st.value(QStringLiteral("t0")).toDouble(), 0, 'f', 2)
                                               .arg(st.value(QStringLiteral("t1")).toDouble(), 0, 'f', 2)},
                 {QStringLiteral("X 轴标签"), st.value(QStringLiteral("xlabel")).toString().isEmpty()
                                                   ? QStringLiteral("—")
                                                   : st.value(QStringLiteral("xlabel")).toString()},
                 {QStringLiteral("Y 轴标签"), st.value(QStringLiteral("ylabel")).toString().isEmpty()
                                                   ? QStringLiteral("—")
                                                   : st.value(QStringLiteral("ylabel")).toString()},
                 {QStringLiteral("左 Y 轴"), y1},
                 {QStringLiteral("右 Y 轴"), y2},
                 {QStringLiteral("曲线"), QStringLiteral("%1 条（右轴 %2）").arg(arr.size()).arg(right)},
                 {QStringLiteral("X 网格"), st.value(QStringLiteral("gridX")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("Y 网格"), st.value(QStringLiteral("gridY")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("次网格"), st.value(QStringLiteral("minorGrid")).toBool(false) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("图例"), st.value(QStringLiteral("legend")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("图例位置"), QStringList{QStringLiteral("左上"), QStringLiteral("右上"),
                                                          QStringLiteral("左下"), QStringLiteral("右下")}
                                                     .value(st.value(QStringLiteral("legendPos")).toInt(0))},
                 {QStringLiteral("边框"), st.value(QStringLiteral("box")).toBool(true) ? QStringLiteral("显示") : QStringLiteral("隐藏")},
                 {QStringLiteral("FlipY"), st.value(QStringLiteral("flipY")).toBool(false) ? QStringLiteral("是") : QStringLiteral("否")},
                 {QStringLiteral("坐标轴字号"), QStringLiteral("%1 px").arg(st.value(QStringLiteral("axisFontSize")).toInt(11))},
                 {QStringLiteral("坐标轴线宽"), QStringLiteral("%1 px").arg(st.value(QStringLiteral("axisLineWidth")).toInt(1))},
                 {QStringLiteral("坐标轴颜色"), st.value(QStringLiteral("axisColor")).toString().isEmpty()
                                                   ? QStringLiteral("跟随主题")
                                                   : st.value(QStringLiteral("axisColor")).toString()}},
                {QStringLiteral("标题"), QStringLiteral("X 轴标签"), QStringLiteral("Y 轴标签"),
                 QStringLiteral("X 网格"), QStringLiteral("Y 网格"), QStringLiteral("次网格"),
                 QStringLiteral("图例"), QStringLiteral("图例位置"), QStringLiteral("边框"),
                 QStringLiteral("FlipY"), QStringLiteral("坐标轴字号"),
                 QStringLiteral("坐标轴线宽"), QStringLiteral("坐标轴颜色")},
                1);
    m_propCanvas = c;
    m_propSeriesIndex = -1;
    m_propKind = 1;
}

void PlottingSession::showSeriesProps(PlottingCanvas *c, int index)
{
    if (!c)
        return;
    const QJsonObject st = c->saveState();
    const QJsonArray arr = st.value(QStringLiteral("series")).toArray();
    if (index < 0 || index >= arr.size())
        return;
    const QJsonObject so = arr.at(index).toObject();
    setPropRows(QStringLiteral("曲线 · %1").arg(so.value(QStringLiteral("label")).toString(
                                               so.value(QStringLiteral("name")).toString())),
                {{QStringLiteral("类型"), QStringLiteral("曲线 (Line)")},
                 {QStringLiteral("名称"), so.value(QStringLiteral("name")).toString()},
                 {QStringLiteral("图例"), so.value(QStringLiteral("label")).toString()},
                 {QStringLiteral("单位"), so.value(QStringLiteral("unit")).toString().isEmpty()
                                               ? QStringLiteral("—")
                                               : so.value(QStringLiteral("unit")).toString()},
                 {QStringLiteral("颜色"), so.value(QStringLiteral("color")).toString()},
                 {QStringLiteral("线宽"), QStringLiteral("%1 px").arg(so.value(QStringLiteral("width")).toInt(1))},
                 {QStringLiteral("线型"), QStringList{QStringLiteral("实线"), QStringLiteral("虚线"),
                                                    QStringLiteral("点线"), QStringLiteral("点划线")}
                                               .value(so.value(QStringLiteral("style")).toInt(0))},
                 {QStringLiteral("标记"), QStringList{QStringLiteral("无"), QStringLiteral("圆点"), QStringLiteral("方块"),
                                                        QStringLiteral("菱形"), QStringLiteral("三角"), QStringLiteral("十字"),
                                                        QStringLiteral("叉号")}
                                                   .value(so.value(QStringLiteral("marker")).toInt(0))},
                 {QStringLiteral("标记大小"), QStringLiteral("%1 px").arg(so.value(QStringLiteral("markerSize")).toInt(5))},
                 {QStringLiteral("绑定轴"), so.value(QStringLiteral("axis")).toInt(0) == 1 ? QStringLiteral("右 Y 轴")
                                                                                          : QStringLiteral("左 Y 轴")},
                 {QStringLiteral("可见"), so.value(QStringLiteral("visible")).toBool(true) ? QStringLiteral("是")
                                                                                         : QStringLiteral("否")},
                 {QStringLiteral("点数"), QString::number(so.value(QStringLiteral("points")).toInt())},
                 {QStringLiteral("时间偏移"), QStringLiteral("%1 s").arg(so.value(QStringLiteral("offset")).toDouble(), 0, 'f', 3)},
                 {QStringLiteral("来源"), so.value(QStringLiteral("srcPath")).toString().isEmpty()
                                               ? QStringLiteral("演示 / 脚本生成")
                                               : so.value(QStringLiteral("srcPath")).toString()}},
                {QStringLiteral("图例"), QStringLiteral("颜色"), QStringLiteral("线宽"),
                 QStringLiteral("线型"), QStringLiteral("标记"), QStringLiteral("标记大小"),
                 QStringLiteral("绑定轴"), QStringLiteral("可见"), QStringLiteral("时间偏移")},
                2);
    m_propCanvas = c;
    m_propSeriesIndex = index;
    m_propKind = 2;
}

void PlottingSession::editSeriesProps(PlottingCanvas *c, int index)
{
    if (!c || index < 0)
        return;
    const QJsonObject st = c->seriesState(index);
    if (st.isEmpty())
        return;

    int srcId = -1;
    QString srcPath;
    QVector<const PsChannelInfo *> channels;
    QString alias;
    const int sid = st.value(QStringLiteral("srcId")).toInt(-1);
    const QString path = st.value(QStringLiteral("srcPath")).toString();
    for (const ps::bridge::SourceView &src : ps::bridge::sourceViews(m_mw))
        if (src.id == sid || (!path.isEmpty() && src.path == path))
        {
            srcId = src.id;
            srcPath = src.path;
            channels = src.channels;
            alias = src.alias.isEmpty() ? src.name : src.alias;
            break;
        }

    SeriesEditDialog dlg(st, channels, alias, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QJsonObject patch = dlg.patch();
    const QString op = dlg.transformId();
    const PsChannelInfo *ch = dlg.channel();
    const double off = patch.value(QStringLiteral("offset")).toDouble();
    const QString oldName = st.value(QStringLiteral("name")).toString();

    if (!op.isEmpty() && ch)
    {
        const quint32 n = ch->point_count;
        if (n < 2 || !ch->time || !ch->value)
            return;
        QVector<double> tt(n), vv(n);
        for (quint32 i = 0; i < n; ++i)
        {
            tt[i] = ch->time[i] - off;
            vv[i] = ch->value[i];
        }

        if (op == QLatin1String("lua"))
        {
            ScriptSeries in;
            in.name = dlg.channelName();
            in.time = tt;
            in.value = vv;
            const ScriptResult r = m_mw->extensions().registry().runScript(dlg.scriptText(), {in});
            if (!r.ok || r.outputs.isEmpty())
            {
                QMessageBox::warning(this, QStringLiteral("脚本错误"), r.error.isEmpty()
                    ? QStringLiteral("脚本没有输出") : r.error);
                return;
            }
            const ScriptOutput &out = r.outputs.first();
            QString label = patch.value(QStringLiteral("label")).toString();
            if (!label.endsWith(QStringLiteral("· 脚本")))
                label += QStringLiteral(" · 脚本");
            patch[QStringLiteral("label")] = label;
            patch[QStringLiteral("offset")] = 0.0;
            c->setSeriesSamples(index, out.time, out.value, label);
            c->patchSeries(index, patch);
            showSeriesProps(c, index);
            log(QStringLiteral("曲线脚本变换：%1").arg(label));
            return;
        }

        QVector<double> ot(n), ov(n);
        uint32_t got = 0;
        char err[256] = {0};
        const QJsonObject params = dlg.transformParams();
        const QByteArray json = QJsonDocument(params).toJson(QJsonDocument::Compact);
        const int rc = ps_filter_apply_series(op.toUtf8().constData(), json.constData(),
                                              tt.constData(), vv.constData(), n,
                                              ot.data(), ov.data(), n, &got, err, sizeof err);
        if (rc != 0 && rc != 6)
        {
            QMessageBox::warning(this, QStringLiteral("变换失败"), QString::fromUtf8(err));
            return;
        }
        if (got == 0)
            return;
        tt.resize(got);
        vv = ov;
        vv.resize(got);

        const QString suffix = dlg.transformLabel();
        QString label = patch.value(QStringLiteral("label")).toString();
        if (!label.endsWith(QStringLiteral("· ") + suffix))
            label += QStringLiteral(" · ") + suffix;
        patch[QStringLiteral("label")] = label;
        patch[QStringLiteral("offset")] = 0.0;
        c->setSeriesSamples(index, tt, vv, label);
        c->patchSeries(index, patch);
        showSeriesProps(c, index);
        log(QStringLiteral("曲线变换：%1").arg(label));
        return;
    }

    if (ch && dlg.channelName() != oldName)
        c->setSeriesChannel(index, ch, sid, srcPath.isEmpty() ? path : srcPath, alias, off);
    c->patchSeries(index, patch);
    showSeriesProps(c, index);
}





static QString plotModeLabel(int mode)
{
    static const char *names[] = {"时间序列", "XY", "散点", "地图", "仪表", "罗盘", "频谱"};
    if (mode < 0 || mode >= int(sizeof(names)/sizeof(names[0])))
        return QStringLiteral("未知");
    return QString::fromUtf8(names[mode]);
}

void PlottingSession::setCanvasViewFlag(int flag, bool on)
{
    for (PlottingCanvas *c : pageCanvases())
    {
        if (flag == 0) c->setGridVisible(on);
        else if (flag == 1) c->setBoxVisible(on);
        else if (flag == 2) c->setLegendVisible(on);
        else if (flag == 3) c->setRubberZoomEnabled(on);
    }
}
void PlottingSession::canvasZoom(double factor) { for (PlottingCanvas *c : pageCanvases()) c->zoomBy(factor); }
void PlottingSession::setCanvasTool(int tool) { for (PlottingCanvas *c : pageCanvases()) c->setTool(tool); }
void PlottingSession::canvasAutoY() { for (PlottingCanvas *c : pageCanvases()) c->requestAutoY(); }

QJsonObject PlottingSession::pageState(QWidget *page) const
{
    QJsonObject o;
    if (auto *host = dynamic_cast<PlottingPage *>(page))
        page = host->layout();
    if (auto *c = qobject_cast<PlottingCanvas *>(page))
    {
        o = c->saveState();
        o[QStringLiteral("type")] = QStringLiteral("canvas");
    }
    else if (auto *layout = dynamic_cast<PlottingLayout *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("layout");
        o[QStringLiteral("vertical")] = layout->orientation() == Qt::Vertical;
        QJsonArray sizes;
        for (int v : layout->sizes())
            sizes.append(v);
        o[QStringLiteral("sizes")] = sizes;
        QJsonArray kids;
        for (int i = 0; i < layout->count(); ++i)
            kids.append(pageState(layout->widget(i)));
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *sp = qobject_cast<QSplitter *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("splitter");
        o[QStringLiteral("vertical")] = sp->orientation() == Qt::Vertical;
        QJsonArray sizes;
        for (int v : sp->sizes())
            sizes.append(v);
        o[QStringLiteral("sizes")] = sizes;
        QJsonArray kids;
        for (int i = 0; i < sp->count(); ++i)
            kids.append(pageState(sp->widget(i)));
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *lb = qobject_cast<QLabel *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("label");
        o[QStringLiteral("text")] = lb->text();
    }
    return o;
}


namespace
{
// 属性/值两列文字按主题实时取色：键列弱化、值列正文色
class ThemePropDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        Q_UNUSED(index)
        option->palette.setColor(QPalette::Text, ThemeManager::color(QStringLiteral("text")));
    }
};
} // namespace

void PlottingSession::setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows,
                                  const QStringList &editable, int kind)
{
    m_propCanvas = nullptr;
    m_propSeriesIndex = -1;
    m_propKind = 0;
    if (m_selLabel)
    {
        m_selText = QStringLiteral("当前对象：%1").arg(object);
        m_selLabel->setToolTip(m_selText);
        m_selLabel->setText(m_selLabel->width() > 0
                                ? QFontMetrics(m_selLabel->font())
                                      .elidedText(m_selText, Qt::ElideRight, m_selLabel->width())
                                : m_selText);
    }
    if (!m_propTable)
        return;
    // 程序填充行本身也会触发 itemChanged，这里整体屏蔽，避免把回填当成用户编辑。
    const QSignalBlocker blocker(m_propTable);
    m_propUpdating = true;
    m_propTable->setRowCount(rows.size());
    // 属性值按类型给控件：开关给复选框、数值给可输入下拉、颜色给色块、枚举给下拉。
    auto push = [this](const QString &key, const QString &value) {
        // 控件还在发信号，直接重建行会踩野指针，等事件循环回头再写回画布。
        QTimer::singleShot(0, this, [this, key, value] { applyPropEdit(key, value); });
    };
    auto centered = [](QWidget *inner, QWidget *parent) {
        auto *w = new QWidget(parent);
        w->setStyleSheet(QStringLiteral("QWidget{background:transparent;}"));
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(4, 0, 4, 0);
        l->addWidget(inner);
        l->addStretch(1);
        return w;
    };
    auto leadingNumber = [](const QString &t, int fallback) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("[-+]?\\d+")).match(t);
        bool ok = false;
        const int v = m.hasMatch() ? m.captured(0).toInt(&ok) : 0;
        return (m.hasMatch() && ok) ? v : fallback;
    };
    auto numberCombo = [&, push, leadingNumber](const QString &key, const QString &text, int fallback) {
        QStringList presets;
        if (key == QStringLiteral("坐标轴字号"))
            presets = {QStringLiteral("9"),  QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12"),
                       QStringLiteral("13"), QStringLiteral("14"), QStringLiteral("16"), QStringLiteral("18")};
        else if (key == QStringLiteral("标记大小"))
            presets = {QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                       QStringLiteral("8"), QStringLiteral("10")};
        else
            presets = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")};
        auto *cb = new QComboBox(m_propTable);
        cb->setEditable(true);
        cb->setInsertPolicy(QComboBox::NoInsert);
        cb->addItems(presets);
        cb->setCurrentText(QString::number(leadingNumber(text, fallback)));
        connect(cb->lineEdit(), &QLineEdit::editingFinished, this, [this, key, cb, push] { push(key, cb->currentText()); });
        connect(cb, QOverload<int>::of(&QComboBox::activated), this,
                [this, key, cb, push](int) { push(key, cb->currentText()); });
        return cb;
    };
    auto enumCombo = [&, push](const QString &key, const QString &text) {
        QStringList options;
        if (key == QStringLiteral("图例位置"))
            options = {QStringLiteral("左上"), QStringLiteral("右上"), QStringLiteral("左下"), QStringLiteral("右下")};
        else if (key == QStringLiteral("线型"))
            options = {QStringLiteral("实线"), QStringLiteral("虚线"), QStringLiteral("点线"), QStringLiteral("点划线")};
        else if (key == QStringLiteral("标记"))
            options = {QStringLiteral("无"), QStringLiteral("圆点"), QStringLiteral("方块"), QStringLiteral("菱形"),
                       QStringLiteral("三角"), QStringLiteral("十字"), QStringLiteral("叉号")};
        else
            options = {QStringLiteral("左 Y 轴"), QStringLiteral("右 Y 轴")};
        auto *cb = new QComboBox(m_propTable);
        cb->addItems(options);
        if (cb->currentText() != text)
            cb->setCurrentText(text);
        connect(cb, QOverload<int>::of(&QComboBox::activated), this,
                [this, key, cb, push](int) { push(key, cb->currentText()); });
        return cb;
    };
    auto colorButton = [&, push](const QString &key, const QString &text) {
        auto *btn = new QToolButton(m_propTable);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton{color:#d5dde5;background:transparent;border:1px solid #2d3841;"
            "border-radius:2px;padding:0 6px;}QToolButton:hover{border-color:#f2b134;}"));
        btn->setCursor(Qt::PointingHandCursor);
        const bool themed = text.isEmpty() || text.contains(QStringLiteral("跟随"));
        QColor c = themed ? QColor(QStringLiteral("#3a4854")) : QColor(text);
        auto swatch = [](const QColor &col) {
            QPixmap pm(14, 14);
            pm.fill(col.isValid() ? col : QColor(QStringLiteral("#8b98a5")));
            return QIcon(pm);
        };
        btn->setIcon(swatch(c));
        btn->setText(themed ? QStringLiteral("跟随主题") : c.name());
        connect(btn, &QToolButton::clicked, this, [this, key, btn, swatch, push] {
            const QColor old = QColor(btn->text());
            const QColor sel = QColorDialog::getColor(old.isValid() ? old : Qt::white, this, QStringLiteral("选择颜色"));
            if (sel.isValid())
                push(key, sel.name());
        });
        if (key == QStringLiteral("坐标轴颜色"))
        {
            // 轴色可以回到“跟随主题”，放右键菜单里，避免和按钮文字挤在一起。
            btn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(btn, &QToolButton::customContextMenuRequested, this, [this, key, btn, push](const QPoint &p) {
                QMenu menu(btn);
                auto *reset = menu.addAction(QStringLiteral("跟随主题"));
                if (menu.exec(btn->mapToGlobal(p)) == reset)
                    push(key, QStringLiteral("跟随主题"));
            });
            btn->setToolTip(QStringLiteral("点击选择颜色，右键恢复跟随主题"));
        }
        return btn;
    };

    for (int i = 0; i < rows.size(); ++i)
    {
        const QString key = rows[i].first;
        const QString text = rows[i].second;
        const bool canEdit = editable.contains(key);
        auto *k = new QTableWidgetItem(key);
        k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_propTable->setItem(i, 0, k);

        auto *v = new QTableWidgetItem(text);
        v->setFlags(canEdit ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                            : Qt::ItemIsEnabled);

        QWidget *editor = nullptr;
        if (canEdit)
        {
            if (key == QStringLiteral("X 网格") || key == QStringLiteral("Y 网格") || key == QStringLiteral("次网格") ||
                key == QStringLiteral("边框") || key == QStringLiteral("FlipY") || key == QStringLiteral("可见") ||
                (kind == 1 && key == QStringLiteral("图例")))
            {
                auto *box = new QCheckBox(m_propTable);
                box->setChecked(text.contains(QStringLiteral("显示")) || text.contains(QStringLiteral("是")) ||
                                text == QStringLiteral("true"));
                connect(box, &QCheckBox::toggled, this, [this, key, push](bool on) {
                    push(key, on ? QStringLiteral("显示") : QStringLiteral("隐藏"));
                });
                editor = centered(box, m_propTable);
            }
            else if (key == QStringLiteral("坐标轴字号") || key == QStringLiteral("坐标轴线宽") ||
                     key == QStringLiteral("线宽") || key == QStringLiteral("标记大小"))
            {
                editor = numberCombo(key, text, key == QStringLiteral("坐标轴字号") ? 11 : 1);
            }
            else if (key == QStringLiteral("图例位置") || key == QStringLiteral("线型") ||
                     key == QStringLiteral("标记") || key == QStringLiteral("绑定轴"))
            {
                editor = enumCombo(key, text);
            }
            else if (key == QStringLiteral("坐标轴颜色") || key == QStringLiteral("颜色"))
            {
                editor = colorButton(key, text);
            }
            else if (key == QStringLiteral("时间偏移"))
            {
                auto *sp = new QDoubleSpinBox(m_propTable);
                sp->setDecimals(3);
                sp->setRange(-1e6, 1e6);
                sp->setSuffix(QStringLiteral(" s"));
                sp->setValue(text.split(QLatin1Char(' ')).first().toDouble());
                connect(sp, &QDoubleSpinBox::editingFinished, this,
                        [this, key, sp, push] { push(key, QString::number(sp->value(), 'f', 3)); });
                editor = sp;
            }
        }

        if (editor)
        {
            v->setFlags(Qt::ItemIsEnabled);
            // 单元格控件背景透明，留着文本会透出来叠字
            v->setText(QString());
            v->setToolTip(text);
            m_propTable->setItem(i, 1, v);
            m_propTable->setCellWidget(i, 1, editor);
        }
        else
        {
            // 纯文本属性仍走内联编辑
            v->setFlags(canEdit ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                                : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            v->setToolTip(canEdit ? QStringLiteral("双击编辑，回车立即生效") : text);
            m_propTable->setItem(i, 1, v);
            m_propTable->removeCellWidget(i, 1);
        }
    }
    m_propUpdating = false;
}


namespace
{
// 属性表里存的是带单位的展示文本（"2 px" / "0.5 s"），编辑时只取前导数字。
int leadingInt(const QString &text, int fallback)
{
    const QRegularExpression re(QStringLiteral("[-+]?\\d+"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return fallback;
    bool ok = false;
    const int v = m.captured(0).toInt(&ok);
    return ok ? v : fallback;
}

double leadingDouble(const QString &text, double fallback)
{
    const QRegularExpression re(QStringLiteral("[-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return fallback;
    bool ok = false;
    const double v = m.captured(0).toDouble(&ok);
    return ok ? v : fallback;
}

// 开关类属性接受“显示/是/开/true”与“隐藏/否/关/false”两种写法。
bool switchOn(const QString &text)
{
    return text.contains(QStringLiteral("显示")) || text.contains(QStringLiteral("是")) ||
           text.contains(QStringLiteral("开")) || text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

int indexOfLabel(const QStringList &labels, const QString &text, int fallback)
{
    const int byText = labels.indexOf(text);
    if (byText >= 0)
        return byText;
    return leadingInt(text, fallback);
}
} // namespace

bool PlottingSession::applyPropEdit(const QString &key, const QString &text)
{
    if (!m_propCanvas)
        return false;
    const QString v = text.trimmed();
    auto reject = [this](const QString &reason) {
        log(QStringLiteral("属性未生效：%1").arg(reason));
        if (m_propKind == 2 && m_propCanvas && m_propSeriesIndex >= 0)
            showSeriesProps(m_propCanvas, m_propSeriesIndex);
        else if (m_propKind == 1 && m_propCanvas)
            showAxesProps(m_propCanvas);
        return false;
    };

    if (m_propKind == 2)
    {
        const QJsonObject st = m_propCanvas->seriesState(m_propSeriesIndex);
        if (st.isEmpty())
            return reject(QStringLiteral("曲线已不存在"));
        QJsonObject patch;
        if (key == QStringLiteral("图例"))
            patch[QStringLiteral("label")] = v;
        else if (key == QStringLiteral("颜色"))
        {
            const QColor c(v);
            if (!c.isValid())
                return reject(QStringLiteral("颜色需为 #RRGGBB 或颜色名：%1").arg(v));
            patch[QStringLiteral("color")] = c.name();
        }
        else if (key == QStringLiteral("线宽"))
            patch[QStringLiteral("width")] = leadingInt(v, st.value(QStringLiteral("width")).toInt(1));
        else if (key == QStringLiteral("线型"))
            patch[QStringLiteral("style")] =
                indexOfLabel({QStringLiteral("实线"), QStringLiteral("虚线"), QStringLiteral("点线"),
                             QStringLiteral("点划线")},
                            v, st.value(QStringLiteral("style")).toInt(0));
        else if (key == QStringLiteral("标记"))
            patch[QStringLiteral("marker")] =
                indexOfLabel({QStringLiteral("无"), QStringLiteral("圆点"), QStringLiteral("方块"),
                             QStringLiteral("菱形"), QStringLiteral("三角"), QStringLiteral("十字"),
                             QStringLiteral("叉号")},
                            v, st.value(QStringLiteral("marker")).toInt(0));
        else if (key == QStringLiteral("标记大小"))
            patch[QStringLiteral("markerSize")] = leadingInt(v, st.value(QStringLiteral("markerSize")).toInt(5));
        else if (key == QStringLiteral("绑定轴"))
            patch[QStringLiteral("axis")] = v.contains(QStringLiteral("右")) ? 1 : 0;
        else if (key == QStringLiteral("可见"))
            patch[QStringLiteral("visible")] = switchOn(v);
        else if (key == QStringLiteral("时间偏移"))
            patch[QStringLiteral("offset")] = leadingDouble(v, st.value(QStringLiteral("offset")).toDouble());
        else
            return false;
        // patchSeries 会发 activated，把属性面板跳回坐标轴；这里是程序化改属性，
        // 不打断用户当前选中的曲线，所以只屏蔽这次调用的信号。
        bool ok = false;
        {
            const QSignalBlocker blocker(m_propCanvas);
            ok = m_propCanvas->patchSeries(m_propSeriesIndex, patch);
        }
        if (!ok)
            return reject(QStringLiteral("曲线写入失败"));
        showSeriesProps(m_propCanvas, m_propSeriesIndex);
        log(QStringLiteral("曲线属性已更新 · %1 = %2").arg(key, v));
        return true;
    }

    if (m_propKind == 1)
    {
        const QJsonObject st = m_propCanvas->saveState();
        const QString xLabel = st.value(QStringLiteral("xlabel")).toString();
        const QString yLabel = st.value(QStringLiteral("ylabel")).toString();
        const bool gx = st.value(QStringLiteral("gridX")).toBool(true);
        const bool gy = st.value(QStringLiteral("gridY")).toBool(true);
        const bool gm = st.value(QStringLiteral("minorGrid")).toBool(false);
        if (key == QStringLiteral("标题"))
            m_propCanvas->setTitle(v);
        else if (key == QStringLiteral("X 轴标签"))
            m_propCanvas->setAxisLabels(v, yLabel);
        else if (key == QStringLiteral("Y 轴标签"))
            m_propCanvas->setAxisLabels(xLabel, v);
        else if (key == QStringLiteral("X 网格"))
            m_propCanvas->setGridAxes(switchOn(v), gy, gm);
        else if (key == QStringLiteral("Y 网格"))
            m_propCanvas->setGridAxes(gx, switchOn(v), gm);
        else if (key == QStringLiteral("次网格"))
            m_propCanvas->setGridAxes(gx, gy, switchOn(v));
        else if (key == QStringLiteral("图例"))
            m_propCanvas->setLegendVisible(switchOn(v));
        else if (key == QStringLiteral("图例位置"))
            m_propCanvas->setLegendPosition(
                indexOfLabel({QStringLiteral("左上"), QStringLiteral("右上"), QStringLiteral("左下"),
                              QStringLiteral("右下")},
                             v, st.value(QStringLiteral("legendPos")).toInt(0)));
        else if (key == QStringLiteral("边框"))
            m_propCanvas->setBoxVisible(switchOn(v));
        else if (key == QStringLiteral("FlipY"))
            m_propCanvas->setFlipY(switchOn(v));
        else if (key == QStringLiteral("坐标轴字号"))
            m_propCanvas->setAxisFontSize(leadingInt(v, st.value(QStringLiteral("axisFontSize")).toInt(11)));
        else if (key == QStringLiteral("坐标轴线宽"))
            m_propCanvas->setAxisLineWidth(leadingInt(v, st.value(QStringLiteral("axisLineWidth")).toInt(1)));
        else if (key == QStringLiteral("坐标轴颜色"))
        {
            const QColor c(v);
            if (!v.isEmpty() && !c.isValid() && !v.contains(QStringLiteral("跟随")))
                return reject(QStringLiteral("颜色需为 #RRGGBB 或颜色名：%1").arg(v));
            // 留空或“跟随主题”即清空自定义颜色
            m_propCanvas->setAxisColor(v.isEmpty() || v.contains(QStringLiteral("跟随")) ? QColor() : c);
        }
        else
            return false;
        showAxesProps(m_propCanvas);
        log(QStringLiteral("坐标轴属性已更新 · %1 = %2").arg(key, v));
        return true;
    }

    return false;
}

bool PlottingSession::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_selLabel && m_selLabel && ev->type() == QEvent::Resize)
    {
        const int w = m_selLabel->width();
        const QString shown = w > 0 ? QFontMetrics(m_selLabel->font()).elidedText(m_selText, Qt::ElideRight, w)
                                    : m_selText;
        if (m_selLabel->text() != shown)
            m_selLabel->setText(shown);
    }
    return QWidget::eventFilter(obj, ev);
}

QWidget *PlottingSession::createPropPanel(bool tree)
{
    QWidget *w = new QWidget(m_mw);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    if (tree)
    {
        m_propTree = new QTreeWidget(w);
        m_propTree->setHeaderLabels({QStringLiteral("对象"), QStringLiteral("值")});
        m_propTree->setAlternatingRowColors(true);
        lay->addWidget(m_propTree);
        bindPlotPanels(nullptr, nullptr, nullptr, m_propTree);
    }
    else
    {
        auto *head = new QWidget(w);
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(8, 4, 8, 4);
        m_selLabel = new QLabel(QStringLiteral("未选择"), head);
        // 对象名可能很长（含路径），标签不能反过来把面板最小宽度顶死：
        // 宽度策略设为 Ignored，按当前宽度省略显示，完整内容放 tooltip。
        m_selLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_selLabel->setMinimumWidth(0);
        m_selLabel->installEventFilter(this);
        hl->addWidget(m_selLabel, 1);
        lay->addWidget(head);
        m_propTable = new QTableWidget(0, 2, w);
        m_propTable->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
        m_propTable->horizontalHeader()->setStretchLastSection(true);
        m_propTable->verticalHeader()->hide();
        m_propTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                                     QAbstractItemView::SelectedClicked);
        m_propTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_propTable->setItemDelegate(new ThemePropDelegate(m_propTable));
        lay->addWidget(m_propTable, 1);
        bindPlotPanels(m_selLabel, nullptr, m_propTable, nullptr);
    }
    return w;
}

void PlottingSession::bindPlotPanels(QLabel *sel, QToolButton *edit, QTableWidget *table, QTreeWidget *tree)
{
    Q_UNUSED(edit) // 面板已全内联编辑，不再有“编辑”按钮；保留参数供外部绑定旧接口
    if (sel) m_selLabel = sel;
    if (table) m_propTable = table;
    if (tree) m_propTree = tree;
    if (m_propTable)
    connect(m_propTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_propUpdating || !it || it->column() != 1)
            return;
        auto *key = m_propTable->item(it->row(), 0);
        if (!key)
            return;
        // 提交编辑时编辑器仍持有该 item，直接重建表格会踩到野指针；
        // 等事件循环回过身再应用，同时也避免重入刷新。
        const QString k = key->text();
        const QString v = it->text();
        QTimer::singleShot(0, this, [this, k, v] { applyPropEdit(k, v); });
    });
    if (m_propTable)
    connect(m_propTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!m_propCanvas || m_propKind != 2 || m_propSeriesIndex < 0)
            return;
        // 可写单元格双击进内联编辑；只读行（名称/单位/来源…）打开曲线设置，
        // 换数据列与变换这类内联覆盖不到的入口仍在那里。
        auto *v = m_propTable->item(row, 1);
        if (v && v->flags().testFlag(Qt::ItemIsEditable))
            return;
        editSeriesProps(m_propCanvas, m_propSeriesIndex);
    });
    auto pick = [this](QTreeWidgetItem *it) {
        if (m_propTreeBuilding || !it) return;
        const int kind = it->data(0, Qt::UserRole).toInt();
        PlottingCanvas *c = reinterpret_cast<PlottingCanvas *>(it->data(0, Qt::UserRole + 1).value<quintptr>());
        if (!c) return;
        if (kind == 1) showAxesProps(c); else if (kind == 2) showSeriesProps(c, it->data(0, Qt::UserRole + 2).toInt());
    };
    if (m_propTree)
    {
        connect(m_propTree, &QTreeWidget::currentItemChanged, this, [pick](QTreeWidgetItem *it, QTreeWidgetItem *) { pick(it); });
        connect(m_propTree, &QTreeWidget::itemClicked, this, [pick](QTreeWidgetItem *it, int) { pick(it); });
    }
    setPropRows(QStringLiteral("画布"), {{QStringLiteral("提示"), QStringLiteral("选中绘图页签后显示属性")}, {QStringLiteral("Y 轴"), QStringLiteral("线性 · 滚轮缩放 / 拖动平移 / 双击复位")}});
}


void PlottingSession::reloadCanvasSettings() { for (PlottingCanvas *c : findChildren<PlottingCanvas *>()) c->reloadDisplaySettings(); }


PlottingSession *plottingSession(MainWindow *mw)
{
    auto *existing = mw->findChild<QObject *>(QStringLiteral("ps.plottingSession"));
    if (existing)
        return static_cast<PlottingSession *>(existing);
    auto *c = new PlottingSession(mw);
    c->setObjectName(QStringLiteral("ps.plottingSession"));
    return c;
}
