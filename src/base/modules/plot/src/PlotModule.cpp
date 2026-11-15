#include "PlotModule.h"
#include "MainWindow.h"
#include "CanvasWidget.h"
#include "PlotGrid.h"
#include "PlotWorkspace.h"
#include <QGridLayout>
#include <QSplitter>
#include <QTimer>
#include "ps_kernel.h"
#include <QInputDialog>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonObject>
#include <QTabBar>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ps::components
{
namespace
{
struct PlotRegistrar
{
    PlotRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("plot.core"), QStringLiteral("绘图"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<PlotComponent>(); });
    }
};
static PlotRegistrar g_plotRegistrar;
} // namespace

ComponentManifest PlotComponent::manifest() const
{
    return {QStringLiteral("plot.core"), QStringLiteral("绘图"), QStringLiteral("1.0"),
            {QStringLiteral("plot"), QStringLiteral("dock")}};
}

void PlotComponent::contribute(Registry &registry, const HostContext &)
{
    registerPlotContributions(registry);
}

void registerPlotContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2,
                                      QStringLiteral(":/icons/prop.svg"), nullptr});
    registry.addRibbon(RibbonContribution{QStringLiteral("绘图"), {}, {},
        {{QStringLiteral("页面"),
             {{QStringLiteral("page.new"), QStringLiteral("新建页"), {}, true},
              {QStringLiteral("page.addCanvas"), QStringLiteral("添加画布"), {}, true},
              {QStringLiteral("page.splitHorizontal"), QStringLiteral("向右拆分"), {}, true},
              {QStringLiteral("page.splitVertical"), QStringLiteral("向下拆分"), {}, true}}},
         {QStringLiteral("画布布局"), {}, QStringLiteral("layoutPicker")},
         {QStringLiteral("显示"), {}, QStringLiteral("viewToggles")},
         {QStringLiteral("图表类型"), {}, QStringLiteral("plotGallery")}},
        2});
}

} // namespace ps::components

void collectPageCanvases(QWidget *root, QVector<CanvasWidget *> &out)
{
    if (!root)
        return;
    if (auto *c = dynamic_cast<CanvasWidget *>(root))
    {
        out.append(c);
        return;
    }
    if (auto *g = dynamic_cast<PlotGrid *>(root))
    {
        out += g->canvases();
        return;
    }
    // 按 splitter 子控件顺序递归，保证“第一个画布”就是屏幕左上角那块，
    // 而不是 findChildren 的先序/逆序结果。
    if (auto *sp = qobject_cast<QSplitter *>(root))
    {
        for (int i = 0; i < sp->count(); ++i)
            collectPageCanvases(sp->widget(i), out);
        return;
    }
    const auto kids = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : kids)
        collectPageCanvases(w, out);
}


QWidget *buildGridSplitter(const QVector<CanvasWidget *> &canvases, int rows, int cols, QWidget *parent)
{
    rows = std::clamp(rows, 1, 8);
    cols = std::clamp(cols, 1, 8);
    auto *root = new QSplitter(Qt::Vertical, parent);
    root->setHandleWidth(5);
    root->setChildrenCollapsible(false);
    for (int r = 0; r < rows; ++r)
    {
        auto *row = new QSplitter(Qt::Horizontal, root);
        row->setHandleWidth(5);
        row->setChildrenCollapsible(false);
        for (int c = 0; c < cols; ++c)
        {
            const int i = r * cols + c;
            if (i < canvases.size())
                row->addWidget(canvases.at(i));
        }
        root->addWidget(row);
    }
    for (int r = 0; r < rows; ++r)
        root->setStretchFactor(r, 1);
    return root;
}


void MainWindow::createPages()
{
    m_plotWorkspace = new ps::plot::PlotWorkspace(
        [](QWidget *parent) { return new CanvasWidget(parent); }, this);
    m_pages = m_plotWorkspace;
    m_plotWorkspace->createDefaultPages();
    m_plotWorkspace->setTimeMarkers(m_timeMarkers);

    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasActivated, this,
            [this](CanvasWidget *c) { refreshPropTree(c); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasAddRequested, this,
            [this](CanvasWidget *) { addCanvasToPage(); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasSplitRequested, this,
            [this](CanvasWidget *c, int orientation) {
                splitCanvas(c, orientation == 0 ? Qt::Horizontal : Qt::Vertical);
            });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::addChannelRequested, this,
            [this](CanvasWidget *c) {
                if (m_srcChannels.isEmpty())
                {
                    log(QStringLiteral("先加载数据文件，再添加曲线"));
                    return;
                }
                QStringList names;
                for (const PsChannelInfo *ch : m_srcChannels)
                {
                    const QString n = QString::fromUtf8(ch->name);
                    names.append(ch->unit ? QStringLiteral("%1 [%2]").arg(n, QString::fromUtf8(ch->unit)) : n);
                }
                bool ok = false;
                const QString pick = QInputDialog::getItem(this, QStringLiteral("添加曲线"),
                                                           QStringLiteral("通道"), names, 0, false, &ok);
                if (ok && !pick.isEmpty())
                {
                    c->addChannel(m_srcChannels.value(names.indexOf(pick)));
                    log(QStringLiteral("添加曲线：%1").arg(pick));
                }
            });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasMaximizeRequested, this,
            [this](CanvasWidget *c) { toggleCanvasMaximize(c); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::markerRequested, this,
            [this](double t) { addTimeMarker(t, QString(), true); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::markerRemoveRequested, this,
            [this](double t) { removeTimeMarker(t); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasRemoveRequested, this,
            [this](CanvasWidget *c) { removeCanvas(c); });

    connect(m_pages, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (m_pages->count() > 2 && !m_pages->tabText(idx).isEmpty())
            m_pages->removeTab(idx);
    });

    setCentralWidget(m_pages);
    connect(m_pages, &QTabWidget::currentChanged, this, [this](int i) {
        if (m_maxedCanvas)
            restoreCanvasMaximize();
        refreshPropTree(currentCanvas());
        if (i >= 0 && i == m_pages->count() - 1 && m_pages->tabText(i) == QStringLiteral("＋"))
            newPage();
    });
}

void MainWindow::applyCanvasGrid(int rows, int cols)
{
    QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
    if (!page || dynamic_cast<QLabel *>(page))
    {
        log(QStringLiteral("当前页不是绘图页"));
        return;
    }
    QVector<CanvasWidget *> canvases;
    collectPageCanvases(page, canvases);
    if (canvases.isEmpty())
    {
        log(QStringLiteral("当前页没有画布"));
        return;
    }

    const int idx = m_pages->currentIndex();
    const QString tabText = m_pages->tabText(idx);
    auto *root = buildGridSplitter(canvases, rows, cols, m_pages);
    m_pages->removeTab(idx);
    m_pages->insertTab(idx, root, tabText);
    m_pages->setCurrentIndex(idx);
    page->deleteLater();
    log(QStringLiteral("页内布局：%1 × %2（可继续拆分）").arg(rows).arg(cols));
}

void MainWindow::toggleCanvasMaximize(CanvasWidget *c)
{
    if (m_maxedCanvas == c)
    {
        restoreCanvasMaximize();
        return;
    }
    if (m_maxedCanvas)
        restoreCanvasMaximize();
    if (!c || !m_pages || !m_pages->isAncestorOf(c))
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

void MainWindow::splitCanvas(CanvasWidget *target, Qt::Orientation orientation)
{
    QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
    if (!page || !target)
        return;
    if (dynamic_cast<QLabel *>(page))
        return;

    // 网格只是“一次摆好”的快捷方式；拆分前先变成 splitter 树，之后就能任意再分
    if (dynamic_cast<PlotGrid *>(page) || !target->parentWidget() ||
        (target->parentWidget() != page && !qobject_cast<QSplitter *>(target->parentWidget())))
    {
        QVector<CanvasWidget *> canvases;
        collectPageCanvases(page, canvases);
        const int rows = dynamic_cast<PlotGrid *>(page) ? dynamic_cast<PlotGrid *>(page)->rows() : 1;
        const int cols = dynamic_cast<PlotGrid *>(page) ? dynamic_cast<PlotGrid *>(page)->cols() : 1;
        const int idx = m_pages->currentIndex();
        const QString tabText = m_pages->tabText(idx);
        auto *root = buildGridSplitter(canvases, rows, cols, m_pages);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, root, tabText);
        m_pages->setCurrentIndex(idx);
        page->deleteLater();
        page = root;
    }

    auto *c = new CanvasWidget(m_pages);
    c->clearChannels();
    c->setTitle(QStringLiteral("新画布 · plot"));
    c->setTimeRange(target->t0(), target->t1());
    m_plotWorkspace->attachCanvas(c);

    if (target->parentWidget() == page && qobject_cast<CanvasWidget *>(page) == target)
    {
        auto *sp = new QSplitter(orientation, m_pages);
        sp->setHandleWidth(5);
        const int idx = m_pages->currentIndex();
        const QString tabText = m_pages->tabText(idx);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, sp, tabText);
        m_pages->setCurrentIndex(idx);
        sp->addWidget(target);
        sp->addWidget(c);
        QTimer::singleShot(0, sp, [sp] {
            QList<int> eq;
            for (int i = 0; i < sp->count(); ++i)
                eq << 1;
            sp->setSizes(eq);
        });
        log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                              : QStringLiteral("上下")));
        return;
    }

    auto *sp = qobject_cast<QSplitter *>(target->parentWidget());
    if (!sp)
        return;
    const int at = sp->indexOf(target);
    if (sp->orientation() == orientation)
    {
        sp->insertWidget(at + 1, c);
    }
    else
    {
        auto *nested = new QSplitter(orientation, sp);
        nested->setHandleWidth(5);
        sp->insertWidget(at, nested);
        nested->addWidget(target);
        nested->addWidget(c);
        nested->setStretchFactor(0, 1);
        nested->setStretchFactor(1, 1);
        QTimer::singleShot(0, nested, [nested] { nested->setSizes({1, 1}); });
    }
    log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                          : QStringLiteral("上下")));
    refreshPropTree(c);
}

void MainWindow::createPlotPage(const QString &kind)
{
    struct PlotKind
    {
        const char *kind;
        const char *label;
        CanvasWidget::PlotMode mode;
    };
    static const PlotKind kinds[] = {
        {"timeseries", "时间序列", CanvasWidget::TimeSeries},
        {"xy", "XY 曲线", CanvasWidget::Xy},
        {"scatter", "散点图", CanvasWidget::Scatter},
        {"bar", "柱状图", CanvasWidget::BarChart},
        {"histogram", "直方图", CanvasWidget::Histogram},
        {"fft", "频谱 FFT", CanvasWidget::Spectrum},
        {"traj3d", "3D 轨迹", CanvasWidget::Traj3D},
        {"map", "GPS 地图", CanvasWidget::GpsMap},
        {"surface3d", "3D 曲面", CanvasWidget::Surface3D},
        {"horizon", "地平仪", CanvasWidget::Horizon},
        {"compass", "罗盘", CanvasWidget::Compass},
        {"gauge", "表盘", CanvasWidget::Gauge},
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
    if (spec->mode == CanvasWidget::TimeSeries)
    {
        newPage();
        if (auto *c = currentCanvas())
        {
            c->setTitle(QStringLiteral("%1 · 选择通道").arg(label));
            log(QStringLiteral("新建图页：%1").arg(label));
        }
        return;
    }

    auto *c = new CanvasWidget(this);
    c->clearChannels();
    c->setTitle(label);
    c->setPlotMode(spec->mode, kind);
    CanvasWidget *seed = nullptr;
    int best = 0;
    for (CanvasWidget *w : findChildren<CanvasWidget *>())
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
        for (const CanvasWidget::SampleSeries &s : seed->sampleSeries())
            c->addGeneratedSeries(s.name, s.time, s.value);
    m_plotWorkspace->attachCanvas(c);
    const int plusIdx = m_pages->count() - 1;
    m_pages->insertTab(plusIdx, c, label);
    m_pages->setCurrentWidget(c);
    log(QStringLiteral("新建图页：%1").arg(label));
}

void MainWindow::addCanvasToPage()
{
    QWidget *page = m_pages->currentWidget();
    if (qobject_cast<QLabel *>(page))
    {
        log(QStringLiteral("该页不是绘图页，先切到绘图页再加画布"));
        return;
    }

    CanvasWidget *target = currentCanvas();
    if (!target)
        target = pageCanvases().value(0, nullptr);
    if (target)
        splitCanvas(target, Qt::Vertical);
    else
        log(QStringLiteral("当前页没有可拆分的画布"));
}

void MainWindow::removeCanvas(CanvasWidget *c)
{
    if (m_maxedCanvas == c)
        restoreCanvasMaximize();
    if (!c)
        return;
    if (auto *sp = qobject_cast<QSplitter *>(c->parentWidget()))
    {
        if (sp->count() <= 1)
        {
            log(QStringLiteral("页内至少保留一个画布"));
            return;
        }
        c->deleteLater();
        log(QStringLiteral("删除画布"));
        refreshPropTree(currentCanvas());
        return;
    }
    log(QStringLiteral("页内至少保留一个画布"));
}

QWidget *MainWindow::buildPage(const QJsonObject &o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas"))
    {
        auto *c = new CanvasWidget(m_pages);
        c->clearChannels();
        QHash<QString, const PsChannelInfo *> pool;
        for (const ps::data_source::SourceData &src : m_sourceStore.items())
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
        auto *g = new PlotGrid(m_pages);
        g->setShape(o.value(QStringLiteral("rows")).toInt(1), o.value(QStringLiteral("cols")).toInt(1));
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            if (auto *c = qobject_cast<CanvasWidget *>(buildPage(v.toObject())))
                g->addCanvas(c);
        return g;
    }
    if (type == QLatin1String("splitter"))
    {
        auto *sp = new QSplitter(o.value(QStringLiteral("vertical")).toBool(true) ? Qt::Vertical
                                                                                 : Qt::Horizontal,
                                 m_pages);
        sp->setHandleWidth(6);
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            sp->addWidget(buildPage(v.toObject()));
        QList<int> sizes;
        for (const QJsonValue &v : o.value(QStringLiteral("sizes")).toArray())
            sizes.append(v.toInt());
        if (!sizes.isEmpty())
            sp->setSizes(sizes);
        return sp;
    }
    return new QLabel(o.value(QStringLiteral("text")).toString(), m_pages);
}

void MainWindow::refreshPropTree(CanvasWidget *active)
{
    if (!m_propTree)
        return;
    QWidget *page = active ? pageForCanvas(active)
                           : (m_pages ? m_pages->currentWidget() : nullptr);
    if (!page || !m_pages || page == m_pages->widget(m_pages->count() - 1))
        return;

    m_propTreeBuilding = true;
    m_propTree->clear();

    const QString pageTitle = m_pages->tabText(m_pages->indexOf(page));
    auto *root = new QTreeWidgetItem(m_propTree);
    root->setText(0, pageTitle);
    root->setIcon(0, QIcon(QStringLiteral(":/icons/panel.svg")));
    root->setFlags(root->flags() & ~Qt::ItemIsSelectable);
    root->setExpanded(true);

    QVector<CanvasWidget *> canvases;
    collectPageCanvases(page, canvases);
    QTreeWidgetItem *selected = nullptr;
    for (CanvasWidget *c : canvases)
    {
        auto *axes = new QTreeWidgetItem(root);
        axes->setText(0, c->title());
        axes->setIcon(0, QIcon(QStringLiteral(":/icons/prop.svg")));
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
        CanvasWidget *c = reinterpret_cast<CanvasWidget *>(
            selected->data(0, Qt::UserRole + 1).value<quintptr>());
        if (c)
            showAxesProps(c);
    }
}

