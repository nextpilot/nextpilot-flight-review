#include "DataSourceModule.h"
#include "MainWindow.h"
#include "DataSourceDock.h"
#include "CanvasWidget.h"
#include "ps_kernel.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ps::components
{
namespace
{
struct DataSourceRegistrar
{
    DataSourceRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("data.source"), QStringLiteral("数据源"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<DataSourceComponent>(); });
    }
};
static DataSourceRegistrar g_dataSourceRegistrar;
} // namespace

ComponentManifest DataSourceComponent::manifest() const
{
    return {QStringLiteral("data.source"), QStringLiteral("数据源"), QStringLiteral("1.0"),
            {QStringLiteral("datasource"), QStringLiteral("dock")}};
}

void DataSourceComponent::contribute(Registry &registry, const HostContext &)
{
    registerDataSourceContributions(registry);
}

void registerDataSourceContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.src"), QStringLiteral("数据源"),
                                      Qt::LeftDockWidgetArea, true, 0,
                                      QStringLiteral(":/icons/datasource.svg"), nullptr});

    registry.addRibbon(RibbonContribution{QStringLiteral("数据"), {}, {},
        {{QStringLiteral("常用"),
             {{QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"), {}, true},
              {QStringLiteral("data.recent"), QStringLiteral("最近"), {}, false},
              {QStringLiteral("data.exportSnapshot"), QStringLiteral("导出快照"), {}, false},
              {QStringLiteral("data.reload"), QStringLiteral("重载"), {}, false}}},
         {QStringLiteral("来源"), {}, QStringLiteral("sourceGallery")}},
        1});
}

} // namespace ps::components

QDockWidget *MainWindow::createDataSourceDock(QWidget *parent, const QString &icon)
{
    // 左：数据源（对齐原型 d-src）：源列表 → 字段/参数/消息 三页签 → 内容
    auto *srcDock = new QDockWidget(QStringLiteral("数据源"), this);
    srcDock->setObjectName(QStringLiteral("dock.src"));
    if (!icon.isEmpty())
        srcDock->setProperty("psIcon", icon);

    // 标题栏：标题 + 汇总 + 全局起停 + 关闭，全部放一行，正文从源列表直接开始（不再叠第二行头部）
    auto *srcBar = new QWidget(srcDock);
    srcBar->setStyleSheet(QStringLiteral(
        "background:#26303a;border-bottom:1px solid #3a4854;"));
    auto *barLay = new QHBoxLayout(srcBar);
    barLay->setContentsMargins(10, 0, 6, 0);
    barLay->setSpacing(6);
    auto *barIcon = new QLabel(srcBar);
    barIcon->setPixmap(QIcon(srcDock->property("psIcon").toString()).pixmap(14, 14));
    barIcon->setStyleSheet(QStringLiteral("background:transparent;"));
    barIcon->setFixedSize(16, 16);
    barLay->addWidget(barIcon);
    auto *barTitle = new QLabel(QStringLiteral("数据源"), srcBar);
    barTitle->setStyleSheet(QStringLiteral(
        "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
    barLay->addWidget(barTitle);
    m_srcCount = new QLabel(QStringLiteral("0 源 · 0 已连接"), srcBar);
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
    barLay->addWidget(m_srcCount);
    barLay->addStretch();
    auto mkTiny = [&](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(srcBar);
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
        b->setIconSize(QSize(14, 14));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(22, 22);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#8494a0;padding:0;}"
            "QToolButton:hover{background:#222b33;}"));
        barLay->addWidget(b);
        return b;
    };
    auto *addSrcBtn = mkTiny(QStringLiteral("newpage"), QStringLiteral("添加数据源"));
    QObject::connect(addSrcBtn, &QToolButton::clicked, this, [this] {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("添加数据源"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    auto *dockMenuBtn = new QToolButton(srcBar);
    dockMenuBtn->setText(QStringLiteral("▾"));
    dockMenuBtn->setToolTip(QStringLiteral("最大化 / 最小化 / 浮动 / 关闭"));
    dockMenuBtn->setCursor(Qt::PointingHandCursor);
    dockMenuBtn->setFixedSize(30, 26);
    dockMenuBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
        "padding:0 0 2px 0;font-size:15px;font-weight:600;}"
        "QToolButton:hover{background:#222b33;border-color:#f2b134;color:#f2b134;}"));
    auto *dockMenu = new QMenu(dockMenuBtn);
    QAction *maxDock = dockMenu->addAction(QStringLiteral("最大化 / 还原"));
    QAction *toLeft = dockMenu->addAction(QStringLiteral("停靠到左侧"));
    QAction *toRight = dockMenu->addAction(QStringLiteral("停靠到右侧"));
    QAction *toBottom = dockMenu->addAction(QStringLiteral("停靠到底部"));
    dockMenu->addSeparator();
    QAction *collapse = dockMenu->addAction(QStringLiteral("最小化（自动隐藏）"));
    QAction *floatAct = dockMenu->addAction(QStringLiteral("浮动 / 取消浮动"));
    dockMenu->addSeparator();
    QAction *closeAct = dockMenu->addAction(QStringLiteral("关闭面板"));
    QObject::connect(maxDock, &QAction::triggered, this, [this, srcDock] { toggleDockMax(srcDock); });
    QObject::connect(toLeft, &QAction::triggered, this, [this, srcDock] { restoreDockMax(); (srcDock->setFloating(false), addDockWidget(Qt::LeftDockWidgetArea, srcDock), srcDock->show()); });
    QObject::connect(toRight, &QAction::triggered, this, [this, srcDock] { (srcDock->setFloating(false), addDockWidget(Qt::RightDockWidgetArea, srcDock), srcDock->show()); });
    QObject::connect(toBottom, &QAction::triggered, this, [this, srcDock] { (srcDock->setFloating(false), addDockWidget(Qt::BottomDockWidgetArea, srcDock), srcDock->show()); });
    QObject::connect(collapse, &QAction::triggered, this, [this, srcDock] {
        restoreDockMax();
        srcDock->hide();
    });
    QObject::connect(floatAct, &QAction::triggered, this, [this, srcDock] {
        restoreDockMax();
        srcDock->setFloating(!srcDock->isFloating());
    });
    QObject::connect(closeAct, &QAction::triggered, this, [this, srcDock] {
        restoreDockMax();
        srcDock->close();
    });
    dockMenuBtn->setPopupMode(QToolButton::InstantPopup);
    QObject::connect(dockMenuBtn, &QToolButton::clicked, dockMenuBtn, [dockMenu, dockMenuBtn] {
        dockMenu->popup(dockMenuBtn->mapToGlobal(QPoint(0, dockMenuBtn->height())));
    });
    barLay->addWidget(dockMenuBtn);
    srcDock->setTitleBarWidget(srcBar);

    auto *panel = new ps::data_source::DataSourceDock(
        ps::data_source::DataSourceDock::Context{
            [this](const QStringList &files) {
                for (const QString &f : files)
                    loadFile(f);
            },
            [this](const QString &id) { connectLiveSource(id); },
            [this](const PsChannelInfo *ch) {
                if (auto *c = currentCanvas(); c && ch)
                    c->addChannel(ch);
            },
            [this](const PsChannelInfo *ch) {
                if (auto *c = currentCanvas(); c && ch)
                {
                    c->setChannels({ch});
                    c->setTitle(QString::fromUtf8(ch->name));
                }
            },
            [this](int id) { return sourceById(id); },
            [this] { return m_activeSource; },
            [this] { refreshActiveSource(); },
            [this](const QString &name) { addTransformFor(name); },
            [this](double t, const QString &topic, bool prompt) {
                addTimeMarker(t, topic, prompt);
            },
            [this](const QString &text) { log(text); },
            [this] { return !m_sourceStore.items().isEmpty(); },
            [this]() -> QVector<SourceData> & { return m_sourceStore.items(); },
            [this](const QString &name) {
                for (auto *c : findChildren<CanvasWidget *>())
                    c->removeChannel(name);
            },
            [this] { return m_srcMsgs; },
            [this](SourceData *src, bool paused) { setSourcePaused(src, paused); },
            [this](SourceData *src) { openSourceDebug(src); },
            [this](SourceData *src) { configureLiveSource(src); },
            [this](QWidget *row) { removeSourceRow(row); },
            [this](QWidget *row) { activateSource(row); },
            [this](QWidget *row) { row->installEventFilter(this); },
            [this](int id, QWidget *row) {
                for (SourceData &src : m_sourceStore.items())
                    if (src.id == id)
                        src.row = row;
            },
            [this](const QString &path) { loadFile(path); },
            [this](SourceData *src, const QString &name) {
                src->alias = name;
                for (auto *c : findChildren<CanvasWidget *>())
                    c->renameSource(src->path, name);
            },
            [this](SourceData *src, int mode) { alignSource(src, mode); },
            [this](SourceData *src) { setSourceBase(src); },
            [this](SourceData *src, double off, bool refit) { setSourceOffset(src, off, refit); },
            [this](SourceData *src, SourceData *other, bool all, const QString &pick) {
                compareSources(src, other, all, pick);
            },
            [this](SourceData *src) { closeLiveSource(src); },
            [this](const QString &path) {
                for (auto *c : findChildren<CanvasWidget *>())
                    c->removeChannelsFromSource(path);
            },
            [this](int id) { m_activeSource = id; },
            [this] {
                m_viewBacked = false;
                m_srcChannels.clear();
                m_srcParams.clear();
                m_srcMsgs.clear();
            },
            [this] { renderSourceData(); },
            [this] { refreshPropTree(currentCanvas()); },
            [this](SourceData &src, bool first) {
                if (first)
                {
                    QVector<const PsChannelInfo *> pick;
                    for (const PsChannelInfo *ch : src.channels)
                    {
                        if (!ch || ch->point_count < 8) continue;
                        double lo = ch->value[0], hi = lo;
                        for (uint32_t k = 1; k < ch->point_count; ++k)
                        {
                            lo = qMin(lo, ch->value[k]);
                            hi = qMax(hi, ch->value[k]);
                        }
                        if (hi - lo > 1e-9) pick.append(ch);
                    }
                    if (pick.isEmpty()) pick = src.channels;
                    const QVector<CanvasWidget *> canvases = pageCanvases();
                    for (int i = 0; i < canvases.size() && i < pick.size(); ++i)
                    {
                        canvases[i]->setInputSource(src.id, src.path, src.alias, src.offsetSec);
                        canvases[i]->setChannels({pick[i]});
                        canvases[i]->setTitle(QStringLiteral("%1 · %2")
                                              .arg(src.alias, QString::fromUtf8(pick[i]->name)));
                    }
                }
            },
            [this](const QString &path) {
                QSettings recentStore(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
                QStringList recent = recentStore.value(QStringLiteral("recentFiles")).toStringList();
                recent.removeAll(path);
                recent.prepend(path);
                while (recent.size() > 12) recent.removeLast();
                recentStore.setValue(QStringLiteral("recentFiles"), recent);
            },
            [this](const QString &msg) { statusBar()->showMessage(msg); },
            [this](SourceData *src) {
                if (!src || src->id != m_activeSource) return;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_liveUiMs < 200 && !m_srcChannels.isEmpty()) return;
                m_liveUiMs = now;
                m_srcChannels = src->channels;
                renderSourceData();
                if (CanvasWidget *c = currentCanvas())
                {
                    c->followLatest();
                    c->notifyActivated();
                }
                for (auto *c : findChildren<CanvasWidget *>()) c->update();
                if (src->plotted || !currentCanvas()) return;
                QVector<const PsChannelInfo *> first;
                auto continuous = [](const PsChannelInfo *ch) {
                    if (!ch || ch->point_count < 2) return false;
                    const QString n = QString::fromUtf8(ch->name ? ch->name : "");
                    for (const QString &skip : {QStringLiteral("status"), QStringLiteral("type"),
                                                QStringLiteral("datum"), QStringLiteral("num_"),
                                                QStringLiteral("id"), QStringLiteral("flag")})
                        if (n.contains(skip, Qt::CaseInsensitive)) return false;
                    return true;
                };
                for (const PsChannelInfo *ch : src->channels)
                {
                    if (continuous(ch)) first.append(ch);
                    if (first.size() >= 2) break;
                }
                if (first.size() < 2)
                {
                    first.clear();
                    for (const PsChannelInfo *ch : src->channels)
                    {
                        if (ch && ch->point_count >= 2) first.append(ch);
                        if (first.size() >= 2) break;
                    }
                }
                if (first.isEmpty()) return;
                if (currentCanvas()->hasData()) newPage();
                if (CanvasWidget *c = currentCanvas())
                    c->setInputSource(src->id, src->path, src->alias, src->offsetSec);
                for (const PsChannelInfo *ch : first) currentCanvas()->addChannel(ch);
                currentCanvas()->setTitle(src->alias);
                currentCanvas()->setFollowLive(true, 30.0);
                src->plotted = true;
            },
            [this](SourceData *src) {
                m_activeSource = src->id;
                m_viewBacked = true;
                m_srcFmt = src->fmt;
                m_srcChannels = src->channels;
                m_srcParams = src->params;
                m_srcMsgs = src->msgs;
                for (auto *c : findChildren<CanvasWidget *>())
                    c->setInputSource(src->id, src->path, src->alias, src->offsetSec);
                renderSourceData();
            },
            [this](QWidget *row) { showSourceProps(row); },
            [this] { updateStatusChips(); },

        },
        this);
    m_dataSourceDock = panel;
    panel->m_srcCount = m_srcCount;
    m_srcCount = panel->m_srcCount;
    m_srcStatus = panel->m_srcStatus;
    m_srcRows = panel->m_srcRows;
    m_srcListToggle = panel->m_srcListToggle;
    m_srcListScroll = panel->m_srcListScroll;
    m_srcTabs = panel->m_srcTabs;
    m_srcFilter = panel->m_srcFilter;
    m_srcStack = panel->m_srcStack;
    m_srcTree = panel->m_srcTree;
    m_paramTable = panel->m_paramTable;
    m_msgTable = panel->m_msgTable;
    m_srcEmpty = panel->m_srcEmpty;
    m_srcChips = panel->m_srcChips;
    srcDock->setWidget(panel);
    return srcDock;
    return srcDock;
    
}
