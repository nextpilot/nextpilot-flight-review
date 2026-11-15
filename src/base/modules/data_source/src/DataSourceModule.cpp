#include "DataSourceModule.h"
#include "MainWindow.h"
#include "DataSourceDock.h"
#include "CanvasWidget.h"
#include "ps_kernel.h"
#include "DataSourceEntry.h"
#include "TransformDialogInterface.h"
#include <QtEndian>
#include "DataSourceRegistry.h"
#include "LiveSession.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QTimer>
#include <QSplitter>
#include <QTreeWidget>
#include <QTableWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDrag>
#include <QMimeData>
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

class ChannelTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-plotscope-channel"), QByteArray::number(ptr));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};
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
    for (const DataSourceSpec &source : registry.dataSources())
        DataSourceRegistry::instance().registerDescriptor(source.descriptor, {}, source.editor);
    registerDataSourceContributions(registry);
}

void registerDataSourceContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.src"), QStringLiteral("数据源"),
                                      Qt::LeftDockWidgetArea, true, 0,
                                      QStringLiteral(":/icons/datasource.svg"),
                                      [](QWidget *parent) -> QWidget * {
                                          auto *mw = qobject_cast<MainWindow *>(parent);
                                          return mw ? mw->createDataSourceDock(parent, QStringLiteral(":/icons/datasource.svg")) : nullptr;
                                      }});

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
    auto *srcBar = createDockTitleBar(srcDock);
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
    QObject::connect(toLeft, &QAction::triggered, this, [this, srcDock] { restoreDockMax(); dockWidgetToArea(srcDock, Qt::LeftDockWidgetArea); });
    QObject::connect(toRight, &QAction::triggered, this, [this, srcDock] { dockWidgetToArea(srcDock, Qt::RightDockWidgetArea); });
    QObject::connect(toBottom, &QAction::triggered, this, [this, srcDock] { dockWidgetToArea(srcDock, Qt::BottomDockWidgetArea); });
    QObject::connect(collapse, &QAction::triggered, this, [this, srcDock] {
        collapseDockWidget(srcDock);
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
    QObject::connect(dockMenuBtn, &QToolButton::clicked, dockMenuBtn, [this, dockMenu, dockMenuBtn] {
        showDockMenu(dockMenuBtn, dockMenu);
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
void MainWindow::showSourceMenu(QWidget *row, const QPoint &globalPos)
{
    m_dataSourceDock->showSourceMenu(row, globalPos);
}

void MainWindow::removeSourceRow(QWidget *row)
{
    m_dataSourceDock->removeSourceRow(row);
}

void MainWindow::updateStatusChips()
{
    if (!m_chipSource)
        return;
    int channels = 0;
    for (const ps::data_source::SourceData &s : m_sourceStore.items())
        channels += s.channels.size();
    if (m_sourceStore.items().isEmpty())
        m_chipSource->setText(QStringLiteral("无数据"));
    else if (std::any_of(m_sourceStore.items().cbegin(), m_sourceStore.items().cend(),
                         [](const ps::data_source::SourceData &s) { return s.live && s.paused; }))
    {
        const int paused = int(std::count_if(m_sourceStore.items().cbegin(), m_sourceStore.items().cend(),
                                             [](const ps::data_source::SourceData &s) { return s.live && s.paused; }));
        m_chipSource->setText(QStringLiteral("%1 源 · %2 暂停").arg(m_sourceStore.items().size()).arg(paused));
    }
    else if (m_sourceStore.items().size() == 1)
        m_chipSource->setText(QStringLiteral("%1 · %2 通道")
                                  .arg(m_sourceStore.items().first().fmt)
                                  .arg(channels));
    else
        m_chipSource->setText(QStringLiteral("%1 源 · %2 通道")
                                  .arg(m_sourceStore.items().size())
                                  .arg(channels));
}

void MainWindow::renderSourceData()
{
    updateStatusChips();
    m_dataSourceDock->renderSourceData(m_srcChannels, m_srcParams, m_srcMsgs,
                                      m_srcFmt, m_viewBacked, m_activeSource);
}

int MainWindow::sourceIdForRow(QWidget *row) const
{
    return row ? row->property("srcId").toInt() : -1;
}

ps::data_source::SourceData *MainWindow::sourceById(int id)
{
    return m_sourceStore.byId(id);
}

namespace {
// ULog 自带 'L'/'C' 文本日志（有真实事件时间），比按 topic 首样本聚合更有意义。
QVector<QStringList> ulogLogMessages(const QString &path, double base)
{
    QVector<QStringList> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray buf = f.readAll();
    if (buf.size() < 24 || std::memcmp(buf.constData(), "ULog", 4) != 0)
        return out;
    int pos = 16;
    while (pos + 3 <= buf.size())
    {
        const quint16 size = qFromLittleEndian<quint16>(buf.constData() + pos);
        const char ty = buf.at(pos + 2);
        const int body = pos + 3;
        if (body + size > buf.size())
            break;
        const char *p = buf.constData() + body;
        if (ty == 'L' && size >= 9)
        {
            const quint64 ts = qFromLittleEndian<quint64>(p + 1);
            QByteArray text(p + 9, size - 9);
            const int nul = text.indexOf('\0');
            if (nul >= 0)
                text.truncate(nul);
            const double t = ts / 1e6 - base;
            if (t >= -0.5)
                out.append({QString::fromUtf8(text), QStringLiteral("—"), QStringLiteral("0"),
                            QString::number(qMax(0.0, t), 'f', 3)});
        }
        else if (ty == 'C' && size >= 11)
        {
            const quint64 ts = qFromLittleEndian<quint64>(p + 3);
            QByteArray text(p + 11, size - 11);
            const int nul = text.indexOf('\0');
            if (nul >= 0)
                text.truncate(nul);
            const double t = ts / 1e6 - base;
            if (t >= -0.5)
                out.append({QString::fromUtf8(text), QStringLiteral("—"), QStringLiteral("0"),
                            QString::number(qMax(0.0, t), 'f', 3)});
        }
        pos = body + size;
    }
    return out;
}

bool sourceTimeRange(const ps::data_source::SourceData *s, double &t0, double &t1)
{
    bool init = false;
    for (const PsChannelInfo *ch : s->channels)
    {
        if (!ch || ch->point_count < 2 || !ch->time)
            continue;
        const double a = ch->time[0];
        const double b = ch->time[ch->point_count - 1];
        if (!init) { t0 = a; t1 = b; init = true; }
        else { t0 = std::min(t0, a); t1 = std::max(t1, b); }
    }
    return init;
}
} // namespace

void MainWindow::setSourceOffset(ps::data_source::SourceData *src, double offsetSec, bool refit)
{
    if (!src)
        return;
    src->offsetSec = offsetSec;
    for (auto *c : findChildren<CanvasWidget *>())
        c->setSourceOffset(src->path, offsetSec, refit);
    if (auto *row = src->row)
        row->setToolTip(QStringLiteral("%1 · 显示偏移 %2 s").arg(src->alias).arg(offsetSec, 0, 'f', 3));
}

void MainWindow::alignSource(ps::data_source::SourceData *src, int mode)
{
    if (!src)
        return;
    double a0 = 0, a1 = 0;
    if (!sourceTimeRange(src, a0, a1))
        return;
    bool init = false;
    double gs = 0, ge = 0;
    for (const ps::data_source::SourceData &o : m_sourceStore.items())
    {
        if (o.id == src->id)
            continue;
        double b0 = 0, b1 = 0;
        if (!sourceTimeRange(&o, b0, b1))
            continue;
        const double ds = b0 - o.offsetSec;
        const double de = b1 - o.offsetSec;
        if (!init) { gs = ds; ge = de; init = true; }
        else { gs = std::min(gs, ds); ge = std::max(ge, de); }
    }
    if (!init)
        return;
    const double target = mode == 0 ? gs : (mode == 2 ? ge : (gs + ge) / 2.0);
    const double center = (a0 + a1) / 2.0;
    const double off = mode == 0 ? a0 - target : (mode == 2 ? a1 - target : center - target);
    setSourceOffset(src, off, true);
    log(QStringLiteral("对齐 %1：%2，偏移 %3 s")
            .arg(src->alias, mode == 0 ? QStringLiteral("起点")
                                       : (mode == 2 ? QStringLiteral("终点") : QStringLiteral("中心")))
            .arg(off, 0, 'f', 3));
}

void MainWindow::setSourceBase(ps::data_source::SourceData *src)
{
    double a0 = 0, a1 = 0;
    if (!src || !sourceTimeRange(src, a0, a1))
        return;
    // 基准源的起点为 0，其余源用同一基准换算（PJ globalTimeReference 语义）
    for (ps::data_source::SourceData &s : m_sourceStore.items())
        setSourceOffset(&s, a0, false);
    for (auto *c : findChildren<CanvasWidget *>())
        for (const ps::data_source::SourceData &s : m_sourceStore.items())
            c->setSourceOffset(s.path, a0, true);
    log(QStringLiteral("时间基准：%1，起点归零，其余源同步换算").arg(src->alias));
}

void MainWindow::compareSources(ps::data_source::SourceData *src, ps::data_source::SourceData *other, bool all, const QString &pick)
{
    if (!src || !other || !currentCanvas())
        return;
    QStringList common;
    for (const PsChannelInfo *a : src->channels)
    {
        if (!a || a->point_count < 2)
            continue;
        const QString n = QString::fromUtf8(a->name);
        for (const PsChannelInfo *b : other->channels)
            if (b && b->point_count > 1 && n == QString::fromUtf8(b->name))
            { common.append(n); break; }
    }
    common.sort();
    if (common.isEmpty())
    {
        log(QStringLiteral("%1 与 %2 没有同名通道").arg(src->alias, other->alias));
        return;
    }
    QStringList names = all ? common : QStringList{pick};
    if (!all)
    {
        bool ok = false;
        const QString n = QInputDialog::getItem(this, QStringLiteral("对比同名通道"),
                                                QStringLiteral("%1 ↔ %2").arg(src->alias, other->alias),
                                                common, qMax(0, common.indexOf(pick)), false, &ok);
        if (!ok || n.isEmpty())
            return;
        names = {n};
    }
    auto *c = currentCanvas();
    int added = 0;
    for (const QString &n : names)
    {
        const PsChannelInfo *a = nullptr;
        const PsChannelInfo *b = nullptr;
        for (const PsChannelInfo *x : src->channels)
            if (x && n == QString::fromUtf8(x->name)) { a = x; break; }
        for (const PsChannelInfo *x : other->channels)
            if (x && n == QString::fromUtf8(x->name)) { b = x; break; }
        if (!a || !b)
            continue;
        c->addChannelEx(a, src->id, src->path, src->alias, src->offsetSec);
        c->addChannelEx(b, other->id, other->path, other->alias, other->offsetSec);
        added += 2;
    }
    log(QStringLiteral("对比 %1 ↔ %2：%3 条曲线").arg(src->alias, other->alias).arg(added));
}







void MainWindow::openSourceDebug(ps::data_source::SourceData *src)
{
    if (!src || !src->live)
        return;
    const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(src->sourceId);
    if (!sp || !sp->send)
    {
        QMessageBox::information(this, QStringLiteral("调试"),
                                 QStringLiteral("该实时源未提供命令通道。"));
        return;
    }
    QVariantMap args;
    args.insert(QStringLiteral("sourceId"), sp->id);
    args.insert(QStringLiteral("name"), src->name);
    args.insert(QStringLiteral("handle"),
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(src->handle)));
    QDialog *dlg = m_extensions.registry().createDialog(QStringLiteral("dialog.device_debug"),
                                                       args, this);
    if (!dlg)
        return;
    dlg->exec();
    delete dlg;
}









void MainWindow::updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor)
{
    if (!row)
        return;
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    if (auto *label = row->findChild<QLabel *>(QStringLiteral("srcMeta")))
        label->setText(meta);
    if (auto *dot = row->findChild<QLabel *>(QStringLiteral("srcDot")))
        dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    refreshSrcCount();
}

void MainWindow::addTransformFor(const QString &channel)
{
    ps::data_source::SourceData *src = sourceById(m_activeSource);
    if (!src || !src->ds)
        return;
    QVariantMap args;
    args.insert(QStringLiteral("dataset"),
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(src->ds)));
    QVariantList channels;
    for (const PsChannelInfo *ch : src->channels)
        channels.append(QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(ch)));
    args.insert(QStringLiteral("channels"), channels);
    args.insert(QStringLiteral("sourceName"), src->alias.isEmpty() ? src->name : src->alias);
    args.insert(QStringLiteral("preselect"), channel);
    QDialog *dlg = m_extensions.registry().createDialog(QStringLiteral("dialog.transform"), args, this);
    if (!dlg)
        return;
    const bool accepted = dlg->exec() == QDialog::Accepted;
    auto *transform = dynamic_cast<ITransformDialog *>(dlg);
    if (!accepted || !transform)
    {
        delete dlg;
        return;
    }
    const TransformDialogResult result = transform->transformResult();
    delete dlg;

    for (const QString &n : result.addedNames)
        if (!src->derived.contains(n))
            src->derived.append(n);
    TransformRecipe r;
    r.kind = result.kind;
    r.input = result.inputName;
    r.filter = result.filterId;
    r.params = result.params;
    r.output = result.outputName;
    r.quat[0] = result.quat[0];
    r.quat[1] = result.quat[1];
    r.quat[2] = result.quat[2];
    r.quat[3] = result.quat[3];
    r.degrees = result.degrees;
    r.unwrap = result.unwrap;
    src->recipes.append(r);
    refreshActiveSource();

    QVector<const PsChannelInfo *> out;
    for (const PsChannelInfo *ch : src->channels)
        if (result.addedNames.contains(QString::fromUtf8(ch->name)))
            out.append(ch);
    if (out.isEmpty())
        return;
    bool replaced = false;
    if (!result.inputName.isEmpty())
        for (auto *c : findChildren<CanvasWidget *>())
            replaced = c->replaceChannel(result.inputName, out.first()) || replaced;
    if (!replaced)
        for (const PsChannelInfo *ch : out)
            if (auto *c = currentCanvas())
                c->addChannel(ch);
    log(QStringLiteral("变换完成：%1").arg(result.addedNames.join(QStringLiteral(", "))));
}







QWidget *MainWindow::addSourceRow(const QString &name, const QString &meta, const QString &dotColor, bool backed,
                              const QString &path, int srcId)
{
    return m_dataSourceDock->addSourceRow(name, meta, dotColor, backed, path, srcId);
}

void MainWindow::refreshSrcCount()
{
    m_dataSourceDock->refreshSrcCount();
}

void MainWindow::syncSrcChips()
{
    m_dataSourceDock->syncSrcChips();
}

void MainWindow::updateSrcStatus()
{
    m_dataSourceDock->updateSrcStatus();
}





void MainWindow::switchSource(int id) { m_dataSourceDock->switchSource(id); }
void MainWindow::reloadChannels(SourceData *src) { m_dataSourceDock->reloadChannels(src); }
void MainWindow::refreshActiveSource() { m_dataSourceDock->refreshActiveSource(); }
void MainWindow::activateSource(QWidget *row) { m_dataSourceDock->activateSource(row); }
void MainWindow::setSourcePaused(SourceData *src, bool paused) { m_dataSourceDock->setSourcePaused(src, paused); }
void MainWindow::exportActiveCsv() { m_dataSourceDock->exportActiveCsv(); }

void MainWindow::showSourceProps(QWidget *row)
{
    if (!row || !m_propTable)
        return;
    ps::data_source::SourceData *src = sourceById(sourceIdForRow(row));
    if (!src)
        return;
    const QString name = src->alias.isEmpty() ? src->name : src->alias;
    int channels = src->live
        ? (src->liveSession ? src->liveSession->channels().size() : 0)
        : src->channels.size();
    qint64 points = 0;
    double duration = 0.0;
    double rate = 0.0;
    if (src->live)
    {
        points = src->liveSession ? src->liveSession->totalSamples() : 0;
    }
    else
    {
        for (const PsChannelInfo *ch : src->channels)
            points += ch->point_count;
    }
    const QVector<const PsChannelInfo *> view =
        src->live ? (src->liveSession ? src->liveSession->channels() : QVector<const PsChannelInfo *>())
                  : src->channels;
    if (!view.isEmpty() && view.first()->point_count > 1)
    {
        const PsChannelInfo *ch = view.first();
        duration = ch->time[ch->point_count - 1] - ch->time[0];
        if (duration > 0.0)
            rate = (ch->point_count - 1) / duration;
    }
    const QString state = row->property("srcMeta").toString();
    const QFileInfo fi(src->path);
    QVector<QPair<QString, QString>> rows;
    rows.append({QStringLiteral("类型"), src->live ? QStringLiteral("实时源") : src->fmt});
    rows.append({QStringLiteral("名称"), name});
    rows.append({QStringLiteral("状态"), state.isEmpty() ? QStringLiteral("—") : state});
    rows.append({QStringLiteral("位置"), src->path.isEmpty() ? src->config : src->path});
    if (src->live)
        rows.append({QStringLiteral("扩展"), src->sourceId});
    rows.append({QStringLiteral("通道"), QString::number(channels)});
    rows.append({QStringLiteral("参数"), QString::number(src->params.size())});
    rows.append({QStringLiteral("消息"), QString::number(src->msgs.size())});
    rows.append({QStringLiteral("采样点"), QString::number(points)});
    if (duration > 0.0)
    {
        rows.append({QStringLiteral("时长"), QStringLiteral("%1 s").arg(duration, 0, 'f', 2)});
        rows.append({QStringLiteral("速率"),
                     rate >= 1000.0 ? QStringLiteral("%1 kHz").arg(rate / 1000.0, 0, 'f', 1)
                                    : QStringLiteral("%1 Hz").arg(rate, 0, 'f', rate >= 10.0 ? 0 : 1)});
    }
    rows.append({QStringLiteral("时间偏移"), QStringLiteral("%1 s").arg(src->offsetSec, 0, 'f', 3)});
    if (fi.exists() && fi.size() > 0)
        rows.append({QStringLiteral("文件大小"),
                     QStringLiteral("%1 MB").arg(fi.size() / 1024.0 / 1024.0, 0, 'f', 2)});
    if (src->live)
        rows.append({QStringLiteral("录制"),
                     src->liveSession && src->liveSession->recording()
                         ? QStringLiteral("进行中 · %1").arg(src->liveSession->recordPath())
                         : QStringLiteral("未录制")});
    m_propTreeBuilding = true;
    if (m_propTree)
    {
        m_propTree->setCurrentItem(nullptr);
        m_propTree->clearSelection();
    }
    m_propTreeBuilding = false;
    setPropRows(QStringLiteral("数据源 · %1").arg(name), rows);
    if (m_propTable->rowCount() > 0 && m_propTable->item(0, 1))
        m_propTable->item(0, 1)->setToolTip(rows.first().second);
}

void MainWindow::applySrcFilter()
{
    if (!m_srcFilter || !m_srcTabs)
        return;
    const QString q = m_srcFilter->text().trimmed();
    const int tab = m_srcTabs->currentIndex();
    // 源内联数据列：源始终显示，按组/叶子过滤
    if (QWidget *host = m_srcRows ? m_srcRows->parentWidget() : nullptr)
        for (auto *panel : host->findChildren<ChannelTree *>())
        {
            if (!panel->property("srcPanel").toBool())
                continue;
            for (int i = 0; i < panel->topLevelItemCount(); ++i)
            {
                QTreeWidgetItem *g = panel->topLevelItem(i);
                const bool groupHit = q.isEmpty() || g->text(0).contains(q, Qt::CaseInsensitive);
                int hits = 0;
                for (int j = 0; j < g->childCount(); ++j)
                {
                    QTreeWidgetItem *leaf = g->child(j);
                    const bool hit = groupHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                    leaf->setHidden(!hit);
                    if (hit)
                        ++hits;
                }
                g->setHidden(q.isEmpty() ? false : (!groupHit && hits == 0));
                if (!q.isEmpty() && hits > 0)
                    g->setExpanded(true);
            }
        }
    if (tab == 0 && m_srcTree)
    {
        for (int i = 0; i < m_srcTree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem *topic = m_srcTree->topLevelItem(i);
            const bool topicHit = q.isEmpty() || topic->text(0).contains(q, Qt::CaseInsensitive);
            int childHit = 0;
            for (int j = 0; j < topic->childCount(); ++j)
            {
                QTreeWidgetItem *leaf = topic->child(j);
                const bool hit = topicHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                if (hit)
                    ++childHit;
            }
            topic->setHidden(q.isEmpty() ? false : (!topicHit && childHit == 0));
            if (!q.isEmpty() && childHit > 0)
                topic->setExpanded(true);
        }
    }
    else if (tab == 1 && m_paramTable)
    {
        for (int r = 0; r < m_paramTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_paramTable->columnCount() && !hit; ++c)
                hit = m_paramTable->item(r, c) && m_paramTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_paramTable->setRowHidden(r, !hit);
        }
    }
    else if (tab == 2 && m_msgTable)
    {
        for (int r = 0; r < m_msgTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_msgTable->columnCount() && !hit; ++c)
                hit = m_msgTable->item(r, c) && m_msgTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_msgTable->setRowHidden(r, !hit);
        }
    }
}

bool MainWindow::loadFile(const QString &path, bool interactive)
{
    return m_dataSourceDock->loadFile(path, interactive);
}

void MainWindow::connectLiveSource(const QString &extensionId)
{
    m_dataSourceDock->connectLiveSource(extensionId);
}

bool MainWindow::openLiveInput(const QStringList &ids, const QString &config, SourceData &dst,
                               QString *fanoutNote, QString *err)
{
    return m_dataSourceDock->openLiveInput(ids, config, dst, fanoutNote, err);
}

void MainWindow::configureLiveSource(SourceData *src) { m_dataSourceDock->configureLiveSource(src); }
void MainWindow::pollLiveSources() { m_dataSourceDock->pollLiveSources(); }
void MainWindow::refreshLiveSource(SourceData *src) { m_dataSourceDock->refreshLiveSource(src); }
void MainWindow::closeLiveSource(SourceData *src) { m_dataSourceDock->closeLiveSource(src); }








void MainWindow::registerDataCommands()
{
    using Ctx = ps::commands::CommandContext;
    m_commands.add(QStringLiteral("data.quickOpen"), [this](const Ctx &) {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("快速打开"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    m_commands.add(QStringLiteral("data.recent"), [this](const Ctx &) {
        QMenu menu(this);
        QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
        const QStringList recent = st.value(QStringLiteral("recentFiles")).toStringList();
        if (recent.isEmpty())
        {
            QAction *empty = menu.addAction(QStringLiteral("暂无最近文件"));
            empty->setEnabled(false);
        }
        for (const QString &path : recent)
        {
            QAction *a = menu.addAction(QFileInfo(path).fileName());
            a->setToolTip(path);
            connect(a, &QAction::triggered, this, [this, path] { loadFile(path); });
        }
        menu.exec(QCursor::pos());
    });
    m_commands.add(QStringLiteral("data.exportSnapshot"), [this](const Ctx &) { exportActiveCsv(); });
    m_commands.add(QStringLiteral("data.reload"), [this](const Ctx &) {
        ps::data_source::SourceData *src = sourceById(m_activeSource);
        if (!src || src->path.isEmpty())
        {
            log(QStringLiteral("当前源没有可重载的文件"));
            return;
        }
        const QString path = src->path;
        QWidget *row = src->row;
        if (row)
            removeSourceRow(row);
        loadFile(path, false);
    });

}
