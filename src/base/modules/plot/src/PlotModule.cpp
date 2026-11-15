#include "PlotModule.h"
#include "MainWindow.h"
#include "CanvasWidget.h"
#include "PlotGrid.h"
#include "DataSourceModule.h"
#include "SourceModel.h"
#include "PlotWorkspace.h"
#include <QGridLayout>
#include <QSplitter>
#include <QTimer>
#include "ps_kernel.h"
#include <QAction>
#include <QHeaderView>
#include "SARibbonPanel.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include <QInputDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QColorDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>

class PlotController;
PlotController *plotController(MainWindow *mw);

class PlotController : public QWidget
{
public:
    explicit PlotController(MainWindow *mw);
    ~PlotController() override;
    void createPages();
    void refreshPropTree(CanvasWidget *active = nullptr);
    CanvasWidget *currentCanvas() const;
    QWidget *pageForCanvas(CanvasWidget *c) const;
    QVector<CanvasWidget *> pageCanvases() const;
    void setCanvasViewFlag(int flag, bool on);
    void canvasZoom(double factor);
    void canvasAutoY();
    void runScript(bool apply = false);
    void bindScriptPanel(QPlainTextEdit *script, QComboBox *inputs[4], QTableWidget *params);
    QTableWidget *propTable() const { return m_propTable; }
    QPlainTextEdit *scriptEditor() const { return m_script; }
    QComboBox **scriptInputs() { return m_scriptInputs; }
    QTableWidget *scriptParams() const { return m_scriptParams; }
    QTreeWidget *propTree() const { return m_propTree; }
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows);
    void bindPlotPanels(QLabel *sel, QToolButton *edit, QTableWidget *table, QTreeWidget *tree);
    QJsonObject pageState(QWidget *page) const;
    QWidget *buildPage(const QJsonObject &o);
    QWidget *createPropPanel(bool tree);
    void applyTimeMarkers();
    void removeSourceFromCanvases(const QString &sourcePath);
    void addChannels(const QVector<const PsChannelInfo *> &channels);
    void newPage();
    void reloadCanvasSettings();
    void createPlotPage(const QString &kind);
    void registerPlotCommands();
    void applyPlayhead(double t);
    void addCanvasToPage();
    void removeCanvas(CanvasWidget *c);
    void applyCanvasGrid(int rows, int cols);
    void splitCanvas(CanvasWidget *target, Qt::Orientation orientation);
    void toggleCanvasMaximize(CanvasWidget *c);
    void restoreCanvasMaximize();
    void showAxesProps(CanvasWidget *c);
    void showSeriesProps(CanvasWidget *c, int index);
    void editSeriesProps(CanvasWidget *c, int index);
    void editAxesProps(CanvasWidget *c);
    void log(const QString &text);

private:
    MainWindow *m_mw = nullptr;
    ps::plot::PlotWorkspace *m_plotWorkspace = nullptr;
    QTableWidget *m_propTable = nullptr;
    QToolButton *m_propEditBtn = nullptr;
    CanvasWidget *m_propCanvas = nullptr;
    int m_propSeriesIndex = -1;
    int m_propKind = 0;
    QTreeWidget *m_propTree = nullptr;
    bool m_propTreeBuilding = false;
    QLabel *m_selLabel = nullptr;
    QLabel *m_runLabel = nullptr;
    QPlainTextEdit *m_script = nullptr;
    QComboBox *m_scriptInputs[4] = {nullptr, nullptr, nullptr, nullptr};
    QTableWidget *m_scriptParams = nullptr;
    CanvasWidget *m_scriptPreviewCanvas = nullptr;
    QStringList m_scriptPreviewNames;
    CanvasWidget *m_maxedCanvas = nullptr;
    QVector<QWidget *> m_maxedHidden;
    QDockWidget *m_maxDock = nullptr;
    QWidget *m_maxDockCentral = nullptr;
    QList<QDockWidget *> m_maxDockHidden;
    QVector<QPair<QSplitter *, QList<int>>> m_maxedSizes;
    QSize m_sizeBeforeMax;
};

static QString plotModeLabel(int mode);
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
    registry.addWindowHook([](QWidget *w) {
        auto *mw = qobject_cast<MainWindow *>(w);
        if (mw) ps::plot::createPages(mw);
    });
}

void registerPlotContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1, nullptr,
                                      [](QWidget *p) {
                                          auto *mw = qobject_cast<MainWindow *>(p);
                                          return mw ? plotController(mw)->createPropPanel(false) : nullptr;
                                      }});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2,
                                      QStringLiteral(":/icons/prop.svg"),
                                      [](QWidget *p) {
                                          auto *mw = qobject_cast<MainWindow *>(p);
                                          return mw ? plotController(mw)->createPropPanel(true) : nullptr;
                                      }});
    registry.addRibbon(RibbonContribution{QStringLiteral("绘图"), {}, {},
        {{QStringLiteral("页面"),
             {{QStringLiteral("page.new"), QStringLiteral("新建页"), {}, true},
              {QStringLiteral("page.addCanvas"), QStringLiteral("添加画布"), {}, true},
              {QStringLiteral("page.splitHorizontal"), QStringLiteral("向右拆分"), {}, true},
              {QStringLiteral("page.splitVertical"), QStringLiteral("向下拆分"), {}, true}}},
         {QStringLiteral("画布布局"), {}, QStringLiteral("layoutPicker")},
         {QStringLiteral("显示"), {}, QStringLiteral("viewToggles"),
             [](QWidget *p) {
                 auto *panel = static_cast<SARibbonPanel *>(p);
                 auto *mw = qobject_cast<MainWindow *>(panel->window());
                 auto add = [&](const QString &text, int flag) {
                     auto *a = new QAction(text, panel);
                     a->setCheckable(true);
                     QObject::connect(a, &QAction::toggled, mw, [mw, flag](bool on) { ps::plot::setCanvasViewFlag(mw, flag, on); });
                     panel->addSmallAction(a);
                 };
                 add(QStringLiteral("网格"), 0);
                 add(QStringLiteral("边框"), 1);
                 add(QStringLiteral("图例"), 2);
                 add(QStringLiteral("框选"), 3);
                 panel->addSeparator();
                 auto *zin = new QAction(QStringLiteral("放大"), panel);
                 QObject::connect(zin, &QAction::triggered, mw, [mw] { ps::plot::canvasZoom(mw, 0.8); });
                 panel->addSmallAction(zin);
                 auto *zout = new QAction(QStringLiteral("缩小"), panel);
                 QObject::connect(zout, &QAction::triggered, mw, [mw] { ps::plot::canvasZoom(mw, 1.25); });
                 panel->addSmallAction(zout);
                 auto *autoY = new QAction(QStringLiteral("自适应"), panel);
                 QObject::connect(autoY, &QAction::triggered, mw, [mw] { ps::plot::canvasAutoY(mw); });
                 panel->addSmallAction(autoY);
             }},
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


void PlotController::removeSourceFromCanvases(const QString &sourcePath)
{
    for (CanvasWidget *c : pageCanvases())
        c->removeChannelsFromSource(sourcePath);
}

void PlotController::addChannels(const QVector<const PsChannelInfo *> &channels)
{
    for (CanvasWidget *c : pageCanvases())
        for (const PsChannelInfo *ch : channels)
            c->addChannel(ch);
}

PlotController::PlotController(MainWindow *mw) : QWidget(mw), m_mw(mw)
{
    registerPlotCommands();
}
PlotController::~PlotController() = default;
void PlotController::log(const QString &text) { m_mw->appendLog(text); }

void PlotController::createPages()
{
    m_plotWorkspace = new ps::plot::PlotWorkspace(
        [](QWidget *parent) { return new CanvasWidget(parent); }, this);
    m_mw->setPageHost(m_plotWorkspace);
    m_plotWorkspace->createDefaultPages();
    m_plotWorkspace->setTimeMarkers(ps::components::dataSourceTimeMarkers(m_mw));

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
                if (ps::components::dataSourceChannels(m_mw).isEmpty())
                {
                    log(QStringLiteral("先加载数据文件，再添加曲线"));
                    return;
                }
                QStringList names;
                for (const PsChannelInfo *ch : ps::components::dataSourceChannels(m_mw))
                {
                    const QString n = QString::fromUtf8(ch->name);
                    names.append(ch->unit ? QStringLiteral("%1 [%2]").arg(n, QString::fromUtf8(ch->unit)) : n);
                }
                bool ok = false;
                const QString pick = QInputDialog::getItem(m_mw, QStringLiteral("添加曲线"),
                                                           QStringLiteral("通道"), names, 0, false, &ok);
                if (ok && !pick.isEmpty())
                {
                    c->addChannel(ps::components::dataSourceChannels(m_mw).value(names.indexOf(pick)));
                    log(QStringLiteral("添加曲线：%1").arg(pick));
                }
            });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasMaximizeRequested, this,
            [this](CanvasWidget *c) { toggleCanvasMaximize(c); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::markerRequested, this,
            [this](double t) { ps::components::addDataSourceTimeMarker(m_mw, t, QString(), true); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::markerRemoveRequested, this,
            [this](double t) { ps::components::removeDataSourceTimeMarker(m_mw, t); });
    connect(m_plotWorkspace, &ps::plot::PlotWorkspace::canvasRemoveRequested, this,
            [this](CanvasWidget *c) { removeCanvas(c); });

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

void PlotController::applyCanvasGrid(int rows, int cols)
{
    QWidget *page = m_mw->pages() ? m_mw->pages()->currentWidget() : nullptr;
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

    const int idx = m_mw->pages()->currentIndex();
    const QString tabText = m_mw->pages()->tabText(idx);
    auto *root = buildGridSplitter(canvases, rows, cols, m_mw->pages());
    m_mw->pages()->removeTab(idx);
    m_mw->pages()->insertTab(idx, root, tabText);
    m_mw->pages()->setCurrentIndex(idx);
    page->deleteLater();
    log(QStringLiteral("页内布局：%1 × %2（可继续拆分）").arg(rows).arg(cols));
}

void PlotController::toggleCanvasMaximize(CanvasWidget *c)
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

void PlotController::splitCanvas(CanvasWidget *target, Qt::Orientation orientation)
{
    QWidget *page = m_mw->pages() ? m_mw->pages()->currentWidget() : nullptr;
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
        const int idx = m_mw->pages()->currentIndex();
        const QString tabText = m_mw->pages()->tabText(idx);
        auto *root = buildGridSplitter(canvases, rows, cols, m_mw->pages());
        m_mw->pages()->removeTab(idx);
        m_mw->pages()->insertTab(idx, root, tabText);
        m_mw->pages()->setCurrentIndex(idx);
        page->deleteLater();
        page = root;
    }

    auto *c = new CanvasWidget(m_mw->pages());
    c->clearChannels();
    c->setTitle(QStringLiteral("新画布 · plot"));
    c->setTimeRange(target->t0(), target->t1());
    m_plotWorkspace->attachCanvas(c);

    if (target->parentWidget() == page && qobject_cast<CanvasWidget *>(page) == target)
    {
        auto *sp = new QSplitter(orientation, m_mw->pages());
        sp->setHandleWidth(5);
        const int idx = m_mw->pages()->currentIndex();
        const QString tabText = m_mw->pages()->tabText(idx);
        m_mw->pages()->removeTab(idx);
        m_mw->pages()->insertTab(idx, sp, tabText);
        m_mw->pages()->setCurrentIndex(idx);
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

void PlotController::createPlotPage(const QString &kind)
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
    const int plusIdx = m_mw->pages()->count() - 1;
    m_mw->pages()->insertTab(plusIdx, c, label);
    m_mw->pages()->setCurrentWidget(c);
    log(QStringLiteral("新建图页：%1").arg(label));
}

void PlotController::addCanvasToPage()
{
    QWidget *page = m_mw->pages()->currentWidget();
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

void PlotController::removeCanvas(CanvasWidget *c)
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

QWidget *PlotController::buildPage(const QJsonObject &o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas"))
    {
        auto *c = new CanvasWidget(m_mw->pages());
        c->clearChannels();
        QHash<QString, const PsChannelInfo *> pool;
        for (const ps::data_source::SourceData &src : ps::components::dataSourceStore(m_mw)->items())
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
        auto *g = new PlotGrid(m_mw->pages());
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
                                 m_mw->pages());
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
    return new QLabel(o.value(QStringLiteral("text")).toString(), m_mw->pages());
}

void PlotController::refreshPropTree(CanvasWidget *active)
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


void PlotController::applyTimeMarkers()
{
    if (m_plotWorkspace)
        m_plotWorkspace->setTimeMarkers(ps::components::dataSourceTimeMarkers(m_mw));
}


void PlotController::newPage()
{
    auto *c = new CanvasWidget(this);
    c->clearChannels();
    c->setTitle(QStringLiteral("新页 · 画布"));
    m_plotWorkspace->attachCanvas(c);
    const int plusIdx = m_mw->pages()->count() - 1;
    m_mw->pages()->insertTab(plusIdx, c, QStringLiteral("新页"));
    m_mw->pages()->setCurrentWidget(c);
    refreshPropTree(c);
    log(QStringLiteral("新建页"));
}

void PlotController::restoreCanvasMaximize()
{
    if (!m_maxedCanvas)
        return;
    CanvasWidget *c = m_maxedCanvas;
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





QWidget *PlotController::pageForCanvas(CanvasWidget *c) const
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

QVector<CanvasWidget *> PlotController::pageCanvases() const
{
    QVector<CanvasWidget *> out;
    if (m_mw->pages())
        collectPageCanvases(m_mw->pages()->currentWidget(), out);
    return out;
}

CanvasWidget *PlotController::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}















void PlotController::registerPlotCommands()
{
    m_mw->commands().add(QStringLiteral("plot.refreshTree"), [this](const auto &) { refreshPropTree(currentCanvas()); });
    m_mw->commands().add(QStringLiteral("script.run"), [this](const auto &ctx) { runScript(ctx.arg == QStringLiteral("true")); });
    m_mw->commands().add(QStringLiteral("plot.setPlayhead"), [this](const auto &ctx) { applyPlayhead(ctx.arg.toDouble()); });
    m_mw->commands().add(QStringLiteral("plot.reloadSettings"), [this](const auto &) { reloadCanvasSettings(); });

    using Ctx = ps::commands::CommandContext;
    m_mw->commands().add(QStringLiteral("page.new"), [this](const Ctx &) { newPage(); });
    m_mw->commands().add(QStringLiteral("page.addCanvas"), [this](const Ctx &) { addCanvasToPage(); });
    m_mw->commands().add(QStringLiteral("page.splitHorizontal"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Horizontal);
    });
    m_mw->commands().add(QStringLiteral("page.splitVertical"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Vertical);
    });

    m_mw->commands().add(QStringLiteral("page.grid"), [this](const Ctx &ctx) {
        const QStringList parts = ctx.arg.split(QLatin1Char(','));
        if (parts.size() == 2)
            applyCanvasGrid(parts.at(0).toInt(), parts.at(1).toInt());
    });

    m_mw->commands().add(QStringLiteral("plot.new"), [this](const Ctx &ctx) {
        createPlotPage(ctx.arg);
    });

}

void PlotController::applyPlayhead(double t)
{
    for (CanvasWidget *c : findChildren<CanvasWidget *>())
    {
        c->setPlayhead(t);
        if (c->plotMode() == CanvasWidget::TimeSeries)
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
            m_color = QColor(QStringLiteral("#4ea1ff"));

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
        m_transform->addItem(QStringLiteral("原值（无变换）"), 0);
        m_transform->addItem(QStringLiteral("微分 dv/dt"), 1);
        m_transform->addItem(QStringLiteral("梯形积分"), 2);
        m_transform->addItem(QStringLiteral("滑动平均"), 3);
        m_transform->addItem(QStringLiteral("绝对值"), 4);
        m_transform->addItem(QStringLiteral("归一化 0–1"), 5);
        m_transform->addItem(QStringLiteral("Lua 脚本"), 6);
        yForm->addRow(QStringLiteral("变换"), m_transform);
        m_window = new QSpinBox(yPage);
        m_window->setRange(2, 500);
        m_window->setValue(5);
        yForm->addRow(QStringLiteral("窗口"), m_window);
        m_script = new QPlainTextEdit(yPage);
        m_script->setPlaceholderText(QStringLiteral("out = ps.movingAvg(ps.input(1), 5)"));
        m_script->setPlainText(QStringLiteral("out = ps.movingAvg(ps.input(1), 5)"));
        m_script->setFixedHeight(78);
        yForm->addRow(QStringLiteral("脚本"), m_script);
        auto *yNote = new QLabel(QStringLiteral("算子/脚本会把结果写成派生曲线；脚本输入为 ps.input(1)，输出到 out。"), yPage);
        yNote->setWordWrap(true);
        yNote->setStyleSheet(QStringLiteral("color:#8494a0;"));
        yForm->addRow(yNote);
        connect(m_transform, &QComboBox::currentIndexChanged, this, [this] {
            const int id = m_transform->currentData().toInt();
            m_window->setEnabled(id == 3);
            m_script->setEnabled(id == 6);
        });
        m_window->setEnabled(false);
        m_script->setEnabled(false);
        tabs->addTab(yPage, QStringLiteral("Y 轴"));

        root->addWidget(tabs);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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
    int transformId() const { return m_transform->currentData().toInt(); }
    int windowSize() const { return m_window->value(); }
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
        m_colorBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:#0d1117;border:1px solid #3a4854;border-radius:2px;"
            "font-weight:600;}QPushButton:hover{border-color:#f2b134;}").arg(m_color.name()));
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
    QSpinBox *m_window = nullptr;
    QPlainTextEdit *m_script = nullptr;
};

class AxesEditDialog : public QDialog
{
public:
    AxesEditDialog(const QJsonObject &st, QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("画布属性"));
        resize(440, 500);
        m_axisColor = QColor(st.value(QStringLiteral("axisColor")).toString());
        auto *root = new QVBoxLayout(this);
        auto *form = new QFormLayout;

        m_title = new QLineEdit(st.value(QStringLiteral("title")).toString(), this);
        form->addRow(QStringLiteral("标题 Title"), m_title);
        m_xLabel = new QLineEdit(st.value(QStringLiteral("xlabel")).toString(), this);
        m_xLabel->setPlaceholderText(QStringLiteral("time / s"));
        form->addRow(QStringLiteral("X 轴标签 XLabel"), m_xLabel);
        m_yLabel = new QLineEdit(st.value(QStringLiteral("ylabel")).toString(), this);
        m_yLabel->setPlaceholderText(QStringLiteral("value"));
        form->addRow(QStringLiteral("Y 轴标签 YLabel"), m_yLabel);

        m_gridX = new QCheckBox(QStringLiteral("X 方向网格"), this);
        m_gridX->setChecked(st.value(QStringLiteral("gridX")).toBool(st.value(QStringLiteral("grid")).toBool(true)));
        form->addRow(QStringLiteral("XGrid"), m_gridX);
        m_gridY = new QCheckBox(QStringLiteral("Y 方向网格"), this);
        m_gridY->setChecked(st.value(QStringLiteral("gridY")).toBool(st.value(QStringLiteral("grid")).toBool(true)));
        form->addRow(QStringLiteral("YGrid"), m_gridY);
        m_minorGrid = new QCheckBox(QStringLiteral("次网格（虚线）"), this);
        m_minorGrid->setChecked(st.value(QStringLiteral("minorGrid")).toBool(false));
        form->addRow(QStringLiteral("MinorGrid"), m_minorGrid);
        m_box = new QCheckBox(QStringLiteral("显示边框"), this);
        m_box->setChecked(st.value(QStringLiteral("box")).toBool(true));
        form->addRow(QStringLiteral("Box"), m_box);
        m_flipY = new QCheckBox(QStringLiteral("翻转 Y 轴方向"), this);
        m_flipY->setChecked(st.value(QStringLiteral("flipY")).toBool(false));
        form->addRow(QStringLiteral("YDir / FlipY"), m_flipY);
        m_legend = new QCheckBox(QStringLiteral("显示图例"), this);
        m_legend->setChecked(st.value(QStringLiteral("legend")).toBool(true));
        form->addRow(QStringLiteral("Legend"), m_legend);
        m_legendPos = new QComboBox(this);
        m_legendPos->addItem(QStringLiteral("左上"), 0);
        m_legendPos->addItem(QStringLiteral("右上"), 1);
        m_legendPos->addItem(QStringLiteral("左下"), 2);
        m_legendPos->addItem(QStringLiteral("右下"), 3);
        m_legendPos->setCurrentIndex(st.value(QStringLiteral("legendPos")).toInt(0));
        form->addRow(QStringLiteral("图例位置"), m_legendPos);

        m_fontSize = new QSpinBox(this);
        m_fontSize->setRange(7, 20);
        m_fontSize->setSuffix(QStringLiteral(" px"));
        m_fontSize->setValue(st.value(QStringLiteral("axisFontSize")).toInt(11));
        form->addRow(QStringLiteral("FontSize"), m_fontSize);
        m_lineWidth = new QSpinBox(this);
        m_lineWidth->setRange(1, 4);
        m_lineWidth->setSuffix(QStringLiteral(" px"));
        m_lineWidth->setValue(st.value(QStringLiteral("axisLineWidth")).toInt(1));
        form->addRow(QStringLiteral("轴线宽 LineWidth"), m_lineWidth);
        m_colorBtn = new QPushButton(this);
        m_colorBtn->setFixedHeight(24);
        setColorButton();
        connect(m_colorBtn, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_axisColor.isValid() ? m_axisColor : QColor("#8b949e"),
                                                    this, QStringLiteral("坐标轴颜色"));
            if (c.isValid())
            {
                m_axisColor = c;
                setColorButton();
            }
        });
        form->addRow(QStringLiteral("轴线颜色"), m_colorBtn);

        root->addLayout(form);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(bb);
    }
    QString title() const { return m_title->text().trimmed(); }
    QString xLabel() const { return m_xLabel->text().trimmed(); }
    QString yLabel() const { return m_yLabel->text().trimmed(); }
    bool gridX() const { return m_gridX->isChecked(); }
    bool gridY() const { return m_gridY->isChecked(); }
    bool minorGrid() const { return m_minorGrid->isChecked(); }
    bool box() const { return m_box->isChecked(); }
    bool flipY() const { return m_flipY->isChecked(); }
    bool legend() const { return m_legend->isChecked(); }
    int legendPosition() const { return m_legendPos->currentData().toInt(); }
    int fontSize() const { return m_fontSize->value(); }
    int lineWidth() const { return m_lineWidth->value(); }
    QColor axisColorValue() const { return m_axisColor; }

private:
    void setColorButton()
    {
        m_colorBtn->setText(m_axisColor.isValid() ? m_axisColor.name() : QStringLiteral("跟随主题"));
        if (m_axisColor.isValid())
            m_colorBtn->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;color:#0d1117;border:1px solid #3a4854;border-radius:2px;"
                "font-weight:600;}QPushButton:hover{border-color:#f2b134;}").arg(m_axisColor.name()));
    }

    QLineEdit *m_title = nullptr;
    QLineEdit *m_xLabel = nullptr;
    QLineEdit *m_yLabel = nullptr;
    QCheckBox *m_gridX = nullptr;
    QCheckBox *m_gridY = nullptr;
    QCheckBox *m_minorGrid = nullptr;
    QCheckBox *m_box = nullptr;
    QCheckBox *m_flipY = nullptr;
    QCheckBox *m_legend = nullptr;
    QComboBox *m_legendPos = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QSpinBox *m_lineWidth = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QColor m_axisColor;
};
void PlotController::showAxesProps(CanvasWidget *c)
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
                                                   : st.value(QStringLiteral("axisColor")).toString()}});
    m_propCanvas = c;
    m_propSeriesIndex = -1;
    m_propKind = 1;
    if (m_propEditBtn)
        m_propEditBtn->setEnabled(true);
}

void PlotController::editAxesProps(CanvasWidget *c)
{
    if (!c)
        return;
    AxesEditDialog dlg(c->saveState(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    c->setTitle(dlg.title());
    c->setAxisLabels(dlg.xLabel(), dlg.yLabel());
    c->setGridAxes(dlg.gridX(), dlg.gridY(), dlg.minorGrid());
    c->setBoxVisible(dlg.box());
    c->setLegendVisible(dlg.legend());
    c->setLegendPosition(dlg.legendPosition());
    c->setFlipY(dlg.flipY());
    c->setAxisFontSize(dlg.fontSize());
    c->setAxisLineWidth(dlg.lineWidth());
    c->setAxisColor(dlg.axisColorValue());
    showAxesProps(c);
}

void PlotController::showSeriesProps(CanvasWidget *c, int index)
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
                                               : so.value(QStringLiteral("srcPath")).toString()}});
    m_propCanvas = c;
    m_propSeriesIndex = index;
    m_propKind = 2;
    if (m_propEditBtn)
        m_propEditBtn->setEnabled(true);
}

void PlotController::editSeriesProps(CanvasWidget *c, int index)
{
    if (!c || index < 0)
        return;
    const QJsonObject st = c->seriesState(index);
    if (st.isEmpty())
        return;

    ps::data_source::SourceData *src = nullptr;
    const int sid = st.value(QStringLiteral("srcId")).toInt(-1);
    const QString path = st.value(QStringLiteral("srcPath")).toString();
    for (ps::data_source::SourceData &sd : ps::components::dataSourceStore(m_mw)->items())
        if (sd.id == sid || (!path.isEmpty() && sd.path == path))
        {
            src = &sd;
            break;
        }
    QVector<const PsChannelInfo *> channels;
    QString alias;
    if (src)
    {
        channels = src->channels;
        alias = src->alias.isEmpty() ? src->name : src->alias;
    }

    SeriesEditDialog dlg(st, channels, alias, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QJsonObject patch = dlg.patch();
    const int op = dlg.transformId();
    const PsChannelInfo *ch = dlg.channel();
    const double off = patch.value(QStringLiteral("offset")).toDouble();
    const QString oldName = st.value(QStringLiteral("name")).toString();

    if (op != 0 && ch)
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
        QString suffix;
        switch (op)
        {
        case 1:
            suffix = QStringLiteral("微分");
            for (quint32 i = n - 1; i > 0; --i)
            {
                const double dt = ch->time[i] - ch->time[i - 1];
                vv[i] = dt > 1e-12 ? (ch->value[i] - ch->value[i - 1]) / dt : 0.0;
            }
            vv[0] = 0.0;
            break;
        case 2:
        {
            suffix = QStringLiteral("积分");
            double acc = 0.0;
            vv[0] = 0.0;
            for (quint32 i = 1; i < n; ++i)
            {
                const double dt = ch->time[i] - ch->time[i - 1];
                acc += (ch->value[i] + ch->value[i - 1]) * 0.5 * dt;
                vv[i] = acc;
            }
            break;
        }
        case 3:
        {
            suffix = QStringLiteral("滑动平均");
            const int w = dlg.windowSize();
            QVector<double> out(n);
            double sum = 0.0;
            for (quint32 i = 0; i < n; ++i)
            {
                sum += vv[i];
                if (int(i) >= w)
                    sum -= vv[i - w];
                out[i] = sum / qMin<int>(int(i) + 1, w);
            }
            vv = out;
            break;
        }
        case 4:
            suffix = QStringLiteral("绝对值");
            for (quint32 i = 0; i < n; ++i)
                vv[i] = std::fabs(vv[i]);
            break;
        case 5:
        {
            suffix = QStringLiteral("归一化");
            double lo = 1e300, hi = -1e300;
            for (double v : vv)
            {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            const double span = hi - lo;
            for (quint32 i = 0; i < n; ++i)
                vv[i] = span > 1e-12 ? (vv[i] - lo) / span : 0.0;
            break;
        }
        case 6:
        {
            suffix = QStringLiteral("脚本");
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
        }
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
        c->setSeriesChannel(index, ch, sid, src ? src->path : path,
                            src ? (src->alias.isEmpty() ? src->name : src->alias) : alias, off);
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

void PlotController::setCanvasViewFlag(int flag, bool on)
{
    for (CanvasWidget *c : pageCanvases())
    {
        if (flag == 0) c->setGridVisible(on);
        else if (flag == 1) c->setBoxVisible(on);
        else if (flag == 2) c->setLegendVisible(on);
        else if (flag == 3) c->setRubberZoomEnabled(on);
    }
}
void PlotController::canvasZoom(double factor) { for (CanvasWidget *c : pageCanvases()) c->zoomBy(factor); }
void PlotController::canvasAutoY() { for (CanvasWidget *c : pageCanvases()) c->requestAutoY(); }

QJsonObject PlotController::pageState(QWidget *page) const
{
    QJsonObject o;
    if (auto *c = qobject_cast<CanvasWidget *>(page))
    {
        o = c->saveState();
        o[QStringLiteral("type")] = QStringLiteral("canvas");
    }
    else if (auto *g = dynamic_cast<PlotGrid *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("grid");
        o[QStringLiteral("rows")] = g->rows();
        o[QStringLiteral("cols")] = g->cols();
        QJsonArray kids;
        for (CanvasWidget *c : g->canvases())
        {
            QJsonObject co = c->saveState();
            co[QStringLiteral("type")] = QStringLiteral("canvas");
            kids.append(co);
        }
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


void PlotController::runScript(bool apply)
{
    QDockWidget *scriptDock = findChild<QDockWidget *>(QStringLiteral("dock.script"));
    CanvasWidget *canvas = nullptr;
    QVector<ScriptSeries> inputs;
    QVector<CanvasWidget *> candidates = pageCanvases();
    if (candidates.isEmpty())
        candidates = findChildren<CanvasWidget *>();
    for (CanvasWidget *c : candidates)
    {
        const auto samples = c->sampleSeries();
        if (samples.isEmpty() || (canvas && inputs.size() >= samples.size()))
            continue;
        canvas = c;
        inputs.clear();
        for (const CanvasWidget::SampleSeries &s : samples)
            inputs.append({s.name, s.time, s.value});
    }
    if (!canvas)
    {
        log(QStringLiteral("脚本：当前页没有可见曲线"));
        return;
    }
    QStringList names;
    for (const ScriptSeries &s : inputs)
        names.append(s.name);
    for (QComboBox *cb : m_scriptInputs)
    {
        if (!cb)
            continue;
        const QString keep = cb->currentData().toString();
        cb->clear();
        cb->addItem(QStringLiteral("（未绑定）"), QString());
        for (const QString &name : names)
            cb->addItem(name, name);
        cb->setCurrentIndex(qMax(0, cb->findData(keep)));
    }

    QVector<ScriptSeries> bound;
    QStringList boundNames;
    for (QComboBox *cb : m_scriptInputs)
    {
        const QString name = cb ? cb->currentData().toString() : QString();
        if (name.isEmpty())
            continue;
        for (const ScriptSeries &s : inputs)
        {
            if (s.name != name)
                continue;
            bound.append(s);
            boundNames.append(QStringLiteral("v%1=%2").arg(bound.size()).arg(name));
            break;
        }
    }
    if (bound.isEmpty())
        bound = inputs;
    log(QStringLiteral("脚本输入：%1").arg(
        boundNames.isEmpty() ? names.join(QStringLiteral(" | ")) : boundNames.join(QStringLiteral(" | "))));
    QMap<QString, double> params;
    if (m_scriptParams)
    {
        for (int row = 0; row < m_scriptParams->rowCount(); ++row)
        {
            const QString key = m_scriptParams->item(row, 0)
                                    ? m_scriptParams->item(row, 0)->text().trimmed()
                                    : QString();
            if (key.isEmpty())
                continue;
            bool ok = false;
            const double v = m_scriptParams->item(row, 1)
                                 ? m_scriptParams->item(row, 1)->text().toDouble(&ok)
                                 : 0.0;
            if (ok)
                params.insert(key, v);
        }
    }
    const ScriptResult r = m_mw->extensions().registry().runScript(m_script->toPlainText(), bound, params);
    if (!r.ok)
    {
        log(QStringLiteral("脚本失败：%1").arg(r.error));
        const QRegularExpression re(QStringLiteral(":(\\d+):"));
        const auto match = re.match(r.error);
        if (match.hasMatch())
        {
            const int line = match.captured(1).toInt();
            QTextCursor cur = m_script->textCursor();
            cur.movePosition(QTextCursor::Start);
            cur.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, qMax(0, line - 1));
            cur.select(QTextCursor::LineUnderCursor);
            m_script->setTextCursor(cur);
        }
        if (scriptDock)
        {
            scriptDock->show();
            scriptDock->raise();
        }
        return;
    }
    if (m_scriptPreviewCanvas)
        for (const QString &name : m_scriptPreviewNames)
            m_scriptPreviewCanvas->removeGeneratedSeries(name);
    m_scriptPreviewCanvas = canvas;
    m_scriptPreviewNames.clear();

    if (apply)
    {
        int idx = 0;
        for (const ScriptOutput &o : r.outputs)
        {
            const QString source = idx < bound.size() ? bound[idx].name : QString();
            ++idx;
            if (!source.isEmpty() && canvas->replaceGeneratedSeries(source, o.name, o.time, o.value))
                log(QStringLiteral("Lua 应用：%1 → %2（继承颜色，%3 点）")
                        .arg(source, o.name).arg(o.value.size()));
            else
            {
                canvas->addGeneratedSeries(o.name, o.time, o.value);
                log(QStringLiteral("Lua 应用：%1（%2 点）").arg(o.name).arg(o.value.size()));
            }
        }
    }
    else
    {
        for (const ScriptOutput &o : r.outputs)
        {
            const QString name = QStringLiteral("preview/%1").arg(o.name);
            canvas->addGeneratedSeries(name, o.time, o.value);
            m_scriptPreviewNames.append(name);
            log(QStringLiteral("Lua 预览：%1（%2 点）").arg(name).arg(o.value.size()));
        }
    }
    log(QStringLiteral("Lua 5.4 %1：%2 条输出")
            .arg(apply ? QStringLiteral("应用完成") : QStringLiteral("预览完成"))
            .arg(r.outputs.size()));
}



void PlotController::bindScriptPanel(QPlainTextEdit *script, QComboBox *inputs[4], QTableWidget *params)
{
    m_script = script;
    for (int i = 0; i < 4; ++i)
        m_scriptInputs[i] = inputs[i];
    m_scriptParams = params;
}


void PlotController::setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows)
{
    m_propCanvas = nullptr;
    m_propSeriesIndex = -1;
    m_propKind = 0;
    if (m_propEditBtn)
        m_propEditBtn->setEnabled(false);
    if (m_selLabel)
        m_selLabel->setText(QStringLiteral("当前对象：%1").arg(object));
    if (!m_propTable)
        return;
    m_propTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i)
    {
        auto *k = new QTableWidgetItem(rows[i].first);
        k->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        auto *v = new QTableWidgetItem(rows[i].second);
        // 属性表当前只做只读展示，避免看起来能改、改完却不生效。
        v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        v->setToolTip(rows[i].second);
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_propTable->setItem(i, 0, k);
        m_propTable->setItem(i, 1, v);
    }
}


QWidget *PlotController::createPropPanel(bool tree)
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
        m_propEditBtn = new QToolButton(head);
        m_propEditBtn->setText(QStringLiteral("编辑"));
        hl->addWidget(m_selLabel, 1);
        hl->addWidget(m_propEditBtn);
        lay->addWidget(head);
        m_propTable = new QTableWidget(0, 2, w);
        m_propTable->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
        m_propTable->horizontalHeader()->setStretchLastSection(true);
        m_propTable->verticalHeader()->hide();
        m_propTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_propTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        lay->addWidget(m_propTable, 1);
        bindPlotPanels(m_selLabel, m_propEditBtn, m_propTable, nullptr);
    }
    return w;
}

void PlotController::bindPlotPanels(QLabel *sel, QToolButton *edit, QTableWidget *table, QTreeWidget *tree)
{
    if (sel) m_selLabel = sel;
    if (edit) m_propEditBtn = edit;
    if (table) m_propTable = table;
    if (tree) m_propTree = tree;
    if (m_propEditBtn)
    connect(m_propEditBtn, &QToolButton::clicked, this, [this] {
        if (!m_propCanvas) return;
        if (m_propKind == 2 && m_propSeriesIndex >= 0) editSeriesProps(m_propCanvas, m_propSeriesIndex);
        else if (m_propKind == 1) editAxesProps(m_propCanvas);
    });
    if (m_propTable)
    connect(m_propTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (!m_propCanvas) return;
        if (m_propKind == 2 && m_propSeriesIndex >= 0) editSeriesProps(m_propCanvas, m_propSeriesIndex);
        else if (m_propKind == 1) editAxesProps(m_propCanvas);
    });
    auto pick = [this](QTreeWidgetItem *it) {
        if (m_propTreeBuilding || !it) return;
        const int kind = it->data(0, Qt::UserRole).toInt();
        CanvasWidget *c = reinterpret_cast<CanvasWidget *>(it->data(0, Qt::UserRole + 1).value<quintptr>());
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


void PlotController::reloadCanvasSettings() { for (CanvasWidget *c : findChildren<CanvasWidget *>()) c->reloadDisplaySettings(); }

PlotController *plotController(MainWindow *mw)
{
    auto *existing = mw->findChild<QObject *>(QStringLiteral("ps.plotController"));
    if (existing)
        return static_cast<PlotController *>(existing);
    auto *c = new PlotController(mw);
    c->setObjectName(QStringLiteral("ps.plotController"));
    return c;
}

namespace ps::plot
{
void createPages(MainWindow *mw) { plotController(mw)->createPages(); }
void refreshPropTree(MainWindow *mw, CanvasWidget *c) { plotController(mw)->refreshPropTree(c); }
CanvasWidget *currentCanvas(MainWindow *mw) { return plotController(mw)->currentCanvas(); }
QVector<CanvasWidget *> pageCanvases(MainWindow *mw) { return plotController(mw)->pageCanvases(); }
void newPage(MainWindow *mw) { plotController(mw)->newPage(); }
void setCanvasViewFlag(MainWindow *mw, int f, bool on) { plotController(mw)->setCanvasViewFlag(f, on); }
void canvasZoom(MainWindow *mw, double f) { plotController(mw)->canvasZoom(f); }
void canvasAutoY(MainWindow *mw) { plotController(mw)->canvasAutoY(); }
void runScript(MainWindow *mw, bool a) { plotController(mw)->runScript(a); }
void reloadCanvasSettings(MainWindow *mw) { plotController(mw)->reloadCanvasSettings(); }
void applyPlayhead(MainWindow *mw, double t) { plotController(mw)->applyPlayhead(t); }
QJsonObject pageState(MainWindow *mw, QWidget *p) { return plotController(mw)->pageState(p); }
QWidget *buildPage(MainWindow *mw, const QJsonObject &o) { return plotController(mw)->buildPage(o); }
void applyTimeMarkers(MainWindow *mw) { plotController(mw)->applyTimeMarkers(); }
void removeSourceFromCanvases(MainWindow *mw, const QString &p) { plotController(mw)->removeSourceFromCanvases(p); }
void addChannels(MainWindow *mw, const QVector<const PsChannelInfo *> &c) { plotController(mw)->addChannels(c); }
void setPropRows(MainWindow *mw, const QString &o, const QVector<QPair<QString, QString>> &r) { plotController(mw)->setPropRows(o, r); }
QTableWidget *propTable(MainWindow *mw) { return plotController(mw)->propTable(); }
QPlainTextEdit *scriptEditor(MainWindow *mw) { return plotController(mw)->scriptEditor(); }
QComboBox **scriptInputs(MainWindow *mw) { return plotController(mw)->scriptInputs(); }
QTableWidget *scriptParams(MainWindow *mw) { return plotController(mw)->scriptParams(); }
QTreeWidget *propTree(MainWindow *mw) { return plotController(mw)->propTree(); }
void bindScriptPanel(MainWindow *mw, QPlainTextEdit *s, QComboBox *i[4], QTableWidget *p) { plotController(mw)->bindScriptPanel(s, i, p); }
void bindPlotPanels(MainWindow *mw, QLabel *s, QToolButton *e, QTableWidget *t, QTreeWidget *tr) { plotController(mw)->bindPlotPanels(s, e, t, tr); }
}
