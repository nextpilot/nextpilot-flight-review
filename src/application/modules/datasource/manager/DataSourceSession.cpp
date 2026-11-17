#include "DataSourceModule.h"
#include "DataSourceSession.h"
#include "ModuleBridge.h"
#include "MainWindow.h"
#include "PropertyManager.h"
#include "ThemeManager.h"
#include "DataSourceDock.h"
#include "ps_kernel.h"
#include "DataSourceEntry.h"
#include "DataSourceModel.h"
#include "TransformDialog.h"
#include "DeviceDebugDialog.h"
#include <QtEndian>
using ps::data_source::DataSource;
using ps::data_source::TransformRecipe;
#include "DataSourceRegistry.h"
#include "LiveFrameSink.h"
#include "DatasetCache.h"

#include <QAction>
#include "SARibbonPanel.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonGalleryItem.h"
#include <QComboBox>
#include <QFileDialog>
#include <QMenu>
#include "SettingsManager.h"
#include "SessionManager.h"
#include <QHash>
#include <QStatusBar>
#include <QInputDialog>
#include <QUuid>
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
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>


class MainWindow;

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

DataSourceSession::DataSourceSession(MainWindow *mw) : QWidget(mw), m_mw(mw)
{
    m_mw->installEventFilter(this);
    m_liveTimer = new QTimer(m_mw);
    m_liveTimer->setInterval(30);
    connect(m_liveTimer, &QTimer::timeout, this, &DataSourceSession::pollLiveSources);
    m_liveTimer->start();
    m_configTimer = new QTimer(this);
    m_configTimer->setSingleShot(true);
    m_configTimer->setInterval(250);
    connect(m_configTimer, &QTimer::timeout, this, &DataSourceSession::persistSourceConfigs);
    restoreSavedConfigs();
    registerDataCommands();
}

DataSourceSession::~DataSourceSession() = default;

void DataSourceSession::log(const QString &text) { m_mw->logs().info(text); }
void DataSourceSession::newPageForward() { ps::bridge::newPage(m_mw); }
QVector<PlottingCanvas *> DataSourceSession::pageCanvasesForward() const { return ps::bridge::pageCanvases(m_mw); }
void DataSourceSession::addTimeMarkerForward(double t, const QString &l, bool p) { addTimeMarker(t,l,p); }
void DataSourceSession::refreshPropTree(PlottingCanvas *c) { ps::bridge::refreshPropTree(m_mw, c); }
PlottingCanvas *DataSourceSession::currentCanvas() const { return ps::bridge::currentCanvas(m_mw); }
std::deque<ps::data_source::DataSource> &DataSourceSession::sources() { return m_sources; }
void DataSourceSession::closeSources() { if (m_liveTimer) m_liveTimer->stop(); }

QWidget *DataSourceSession::createDataSourcePanel(QWidget *parent)
{
    auto *panel = new ps::data_source::DataSourceDock(
        ps::data_source::DataSourceDock::Context{
            [this](const QStringList &files) {
                for (const QString &f : files)
                    loadFile(f);
            },
            [this](const QString &id) { connectLiveSource(id); },
            [this](const PsChannelInfo *ch) {
                if (auto *c = currentCanvas(); c && ch)
                    ps::bridge::canvasAddChannel(c, ch);
            },
            [this](const PsChannelInfo *ch) {
                if (auto *c = currentCanvas(); c && ch)
                {
                    ps::bridge::canvasSetChannels(c, {ch});
                    ps::bridge::canvasSetTitle(c, QString::fromUtf8(ch->name));
                }
            },
            [this](int id) { return sourceById(id); },
            [this] { return m_activeSource; },
            [this] { refreshActiveSource(); },
            [this](const QString &name) { addTransformFor(name); },
            [this](double t, const QString &topic, bool prompt) {
                addTimeMarkerForward(t, topic, prompt);
            },
            [this](const QString &text) { log(text); },
            [this] { return !m_sources.empty(); },
            [this]() -> std::deque<DataSource> & { return m_sources; },
            [this](const QString &name) {
                for (auto *c : ps::bridge::allCanvases(m_mw))
                    ps::bridge::canvasRemoveChannel(c, name);
            },
            [this] { return m_srcMsgs; },
            [this](DataSource *src, bool paused) { setSourcePaused(src, paused); },
            [this](DataSource *src) { openSourceDebug(src); },
            [this](DataSource *src) { configureLiveSource(src); },
            [this](QWidget *row) { removeSourceRow(row); },
            [this](QWidget *row) { activateSource(row); },
            [this](QWidget *row) { row->installEventFilter(this); },
            [this](int id, QWidget *row) {
                for (DataSource &src : m_sources)
                    if (src.config.id == id)
                        src.row = row;
            },
            [this](DataSource *src, const QString &name) {
                src->config.alias = name;
                for (auto *c : ps::bridge::allCanvases(m_mw))
                    ps::bridge::canvasRenameSource(c, src->config.path, name);
                persistSourceConfigsSoon();
            },
            [this](DataSource *src, int mode) { alignSource(src, mode); },
            [this](DataSource *src) { setSourceBase(src); },
            [this](DataSource *src, double off, bool refit) { setSourceOffset(src, off, refit); },
            [this](DataSource *src, DataSource *other, bool all, const QString &pick) {
                compareSources(src, other, all, pick);
            },
            [this](DataSource *src) { closeLiveSource(src); },
            [this](const QString &path) {
                for (auto *c : ps::bridge::allCanvases(m_mw))
                    ps::bridge::canvasRemoveChannelsFromSource(c, path);
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
            [this](DataSource &src, bool first) {
                if (first)
                {
                    QVector<const PsChannelInfo *> pick;
                    for (const PsChannelInfo *ch : src.channels)
                    {
                        if (!ch || ch->point_count < 8) continue;
                        double lo = ps_channel_value_at(ch, 0), hi = lo;
                        for (uint32_t k = 1; k < ch->point_count; ++k)
                        {
                            lo = qMin(lo, ps_channel_value_at(ch, k));
                            hi = qMax(hi, ps_channel_value_at(ch, k));
                        }
                        if (hi - lo > 1e-9) pick.append(ch);
                    }
                    if (pick.isEmpty()) pick = src.channels;
                    const QVector<PlottingCanvas *> canvases = pageCanvasesForward();
                    for (int i = 0; i < canvases.size() && i < pick.size(); ++i)
                    {
                        ps::bridge::canvasSetInputSource(canvases[i], src.config.id, src.config.path, src.config.alias, src.config.offsetSec);
                        ps::bridge::canvasSetChannels(canvases[i], {pick[i]});
                        ps::bridge::canvasSetTitle(canvases[i], QStringLiteral("%1 · %2")
                                              .arg(src.config.alias, QString::fromUtf8(pick[i]->name)));
                    }
                }
            },
            [this](const QString &path) { m_mw->session().addRecentFile(path); },
            [this](const QString &msg) { m_mw->statusBar()->showMessage(msg); },
            [this](DataSource *src) {
                if (!src || src->config.id != m_activeSource) return;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_liveUiMs < 200 && !m_srcChannels.isEmpty()) return;
                m_liveUiMs = now;
                m_srcChannels = src->channels;
                renderSourceData();
                if (PlottingCanvas *c = currentCanvas())
                {
                    ps::bridge::canvasFollowLatest(c);
                    ps::bridge::canvasNotifyActivated(c);
                }
                for (auto *c : ps::bridge::allCanvases(m_mw)) ps::bridge::canvasUpdate(c);
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
                if (ps::bridge::canvasHasData(currentCanvas())) newPageForward();
                if (PlottingCanvas *c = currentCanvas())
                    ps::bridge::canvasSetInputSource(c, src->config.id, src->config.path, src->config.alias, src->config.offsetSec);
                for (const PsChannelInfo *ch : first) ps::bridge::canvasAddChannel(currentCanvas(), ch);
                ps::bridge::canvasSetTitle(currentCanvas(), src->config.alias);
                ps::bridge::canvasSetFollowLive(currentCanvas(), true, 30.0);
                src->plotted = true;
            },
            [this](DataSource *src) {
                m_activeSource = src->config.id;
                m_viewBacked = true;
                m_srcFmt = src->config.fmt;
                m_srcChannels = src->channels;
                m_srcParams = src->params;
                m_srcMsgs = src->msgs;
                for (auto *c : ps::bridge::allCanvases(m_mw))
                    ps::bridge::canvasSetInputSource(c, src->config.id, src->config.path, src->config.alias, src->config.offsetSec);
                renderSourceData();
            },
            [this](QWidget *row) { showSourceProps(row); },
            [this] { updateStatusChips(); },
            [this] { return ps::bridge::plottedChannelColors(m_mw); },

        },
        this);
    m_dataSourceDock = panel;
    panel->m_srcCount = m_srcCount;
    m_srcCount = panel->m_srcCount;
    m_srcRows = panel->m_srcRows;
    m_srcListScroll = panel->m_srcListScroll;
    m_srcTabs = panel->m_srcTabs;
    m_srcFilter = panel->m_srcFilter;
    m_srcStack = panel->m_srcStack;
    m_srcTree = panel->m_srcTree;
    m_paramTable = panel->m_paramTable;
    m_msgTable = panel->m_msgTable;
    m_srcEmpty = panel->m_srcEmpty;
    m_srcChips = panel->m_srcChips;
    return panel;
}
void DataSourceSession::showSourceMenu(QWidget *row, const QPoint &globalPos)
{
    m_dataSourceDock->showSourceMenu(row, globalPos);
}

void DataSourceSession::removeSourceRow(QWidget *row)
{
    m_dataSourceDock->removeSourceRow(row);
    persistSourceConfigs();
}

void DataSourceSession::updateStatusChips()
{
    if (!m_chipSource)
        m_chipSource = m_mw->findChild<QAction *>(QStringLiteral("ps.chip.source"));
    if (!m_chipLod)
        m_chipLod = m_mw->findChild<QAction *>(QStringLiteral("ps.chip.lod"));
    if (!m_chipSource)
        return;
    int channels = 0;
    for (const ps::data_source::DataSource &s : m_sources)
        channels += s.channels.size();
    if (m_sources.empty())
        m_chipSource->setText(QStringLiteral("无数据"));
    else if (std::any_of(m_sources.cbegin(), m_sources.cend(),
                         [](const ps::data_source::DataSource &s) { return s.isLive() && s.paused; }))
    {
        const int paused = int(std::count_if(m_sources.cbegin(), m_sources.cend(),
                                             [](const ps::data_source::DataSource &s) { return s.isLive() && s.paused; }));
        m_chipSource->setText(QStringLiteral("%1 源 · %2 暂停").arg(m_sources.size()).arg(paused));
    }
    else if (m_sources.size() == 1)
        m_chipSource->setText(QStringLiteral("%1 · %2 通道")
                                  .arg(m_sources.front().config.fmt)
                                  .arg(channels));
    else
        m_chipSource->setText(QStringLiteral("%1 源 · %2 通道")
                                  .arg(m_sources.size())
                                  .arg(channels));
}

void DataSourceSession::renderSourceData()
{
    updateStatusChips();
    m_dataSourceDock->renderSourceData(m_srcChannels, m_srcParams, m_srcMsgs,
                                      m_srcFmt, m_viewBacked, m_activeSource);
    refreshPropertyNodes();
}

int DataSourceSession::sourceIdForRow(QWidget *row) const
{
    return row ? row->property("srcId").toInt() : -1;
}

ps::data_source::DataSource *DataSourceSession::sourceById(int id)
{
    for (ps::data_source::DataSource &src : m_sources)
        if (src.config.id == id)
            return &src;
    return nullptr;
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

bool sourceTimeRange(const ps::data_source::DataSource *s, double &t0, double &t1)
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

void DataSourceSession::setSourceOffset(ps::data_source::DataSource *src, double offsetSec, bool refit)
{
    if (!src)
        return;
    src->config.offsetSec = offsetSec;
    for (auto *c : ps::bridge::allCanvases(m_mw))
        ps::bridge::canvasSetSourceOffset(c, src->config.path, offsetSec, refit);
    if (auto *row = src->row)
        row->setToolTip(QStringLiteral("%1 · 显示偏移 %2 s").arg(src->config.alias).arg(offsetSec, 0, 'f', 3));
    persistSourceConfigsSoon();
}

void DataSourceSession::alignSource(ps::data_source::DataSource *src, int mode)
{
    if (!src)
        return;
    double a0 = 0, a1 = 0;
    if (!sourceTimeRange(src, a0, a1))
        return;
    bool init = false;
    double gs = 0, ge = 0;
    for (const ps::data_source::DataSource &o : m_sources)
    {
        if (o.config.id == src->config.id)
            continue;
        double b0 = 0, b1 = 0;
        if (!sourceTimeRange(&o, b0, b1))
            continue;
        const double ds = b0 - o.config.offsetSec;
        const double de = b1 - o.config.offsetSec;
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
            .arg(src->config.alias, mode == 0 ? QStringLiteral("起点")
                                       : (mode == 2 ? QStringLiteral("终点") : QStringLiteral("中心")))
            .arg(off, 0, 'f', 3));
}

void DataSourceSession::setSourceBase(ps::data_source::DataSource *src)
{
    double a0 = 0, a1 = 0;
    if (!src || !sourceTimeRange(src, a0, a1))
        return;
    // 基准源的起点为 0，其余源用同一基准换算（PJ globalTimeReference 语义）
    for (ps::data_source::DataSource &s : m_sources)
        setSourceOffset(&s, a0, false);
    for (auto *c : ps::bridge::allCanvases(m_mw))
        for (const ps::data_source::DataSource &s : m_sources)
            ps::bridge::canvasSetSourceOffset(c, s.config.path, a0, true);
    log(QStringLiteral("时间基准：%1，起点归零，其余源同步换算").arg(src->config.alias));
}

void DataSourceSession::compareSources(ps::data_source::DataSource *src, ps::data_source::DataSource *other, bool all, const QString &pick)
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
        log(QStringLiteral("%1 与 %2 没有同名通道").arg(src->config.alias, other->config.alias));
        return;
    }
    QStringList names = all ? common : QStringList{pick};
    if (!all)
    {
        bool ok = false;
        const QString n = QInputDialog::getItem(this, QStringLiteral("对比同名通道"),
                                                QStringLiteral("%1 ↔ %2").arg(src->config.alias, other->config.alias),
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
        ps::bridge::canvasAddChannelEx(c, a, src->config.id, src->config.path, src->config.alias, src->config.offsetSec);
        ps::bridge::canvasAddChannelEx(c, b, other->config.id, other->config.path, other->config.alias, other->config.offsetSec);
        added += 2;
    }
    log(QStringLiteral("对比 %1 ↔ %2：%3 条曲线").arg(src->config.alias, other->config.alias).arg(added));
}







void DataSourceSession::openSourceDebug(ps::data_source::DataSource *src)
{
    if (!src || !src->isLive())
        return;
    const DataSourceEntry *sp = DataSourceRegistry::instance().findSourceById(src->config.extensionId);
    if (!sp || !sp->send)
    {
        QMessageBox::information(this, QStringLiteral("调试"),
                                 QStringLiteral("该实时源未提供命令通道。"));
        return;
    }
    void *handle = src->handle;
    auto *dlg = new DeviceDebugDialog(
        *sp, src->config.name,
        [sp, handle] { return handle ? sp->status(handle) : QString(); },
        [sp, handle](const QString &cmd) {
            return handle ? sp->send(handle, cmd)
                          : QStringLiteral("{\"ok\":false,\"error\":\"未连接\"}");
        },
        this);
    dlg->exec();
    delete dlg;
}









void DataSourceSession::updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor)
{
    if (!row)
        return;
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    row->setProperty("srcState", dotColor == QStringLiteral("#4a545d") ? QStringLiteral("off")
                                   : dotColor == QStringLiteral("#39c5cf") ? QStringLiteral("live")
                                                                           : QStringLiteral("ok"));
    row->style()->unpolish(row);
    row->style()->polish(row);
    if (auto *label = row->findChild<QLabel *>(QStringLiteral("srcMeta")))
        label->setText(meta);
    refreshSrcCount();
}

void DataSourceSession::addTransformFor(const QString &channel)
{
    ps::data_source::DataSource *src = sourceById(m_activeSource);
    if (!src || !src->ds)
        return;
    QVector<const PsChannelInfo *> channels;
    for (const PsChannelInfo *ch : src->channels)
        channels.append(ch);
    auto *dlg = new TransformDialog(src->ds, channels,
                                    src->config.alias.isEmpty() ? src->config.name : src->config.alias,
                                    channel, this);
    const bool accepted = dlg->exec() == QDialog::Accepted;
    if (!accepted)
    {
        delete dlg;
        return;
    }
    const TransformDialogResult result = dlg->transformResult();
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
        for (auto *c : ps::bridge::allCanvases(m_mw))
            replaced = ps::bridge::canvasReplaceChannel(c, result.inputName, out.first()) || replaced;
    if (!replaced)
        for (const PsChannelInfo *ch : out)
            if (auto *c = currentCanvas())
                ps::bridge::canvasAddChannel(c, ch);
    log(QStringLiteral("变换完成：%1").arg(result.addedNames.join(QStringLiteral(", "))));
}







QWidget *DataSourceSession::addSourceRow(const QString &name, const QString &meta, const QString &dotColor, bool backed,
                              const QString &path, int srcId)
{
    return m_dataSourceDock->addSourceRow(name, meta, dotColor, backed, path, srcId);
}

void DataSourceSession::refreshSrcCount()
{
    m_dataSourceDock->refreshSrcCount();
}

void DataSourceSession::syncSrcChips()
{
    m_dataSourceDock->syncSrcChips();
}

void DataSourceSession::updateSrcStatus()
{
    m_dataSourceDock->updateSrcStatus();
}





void DataSourceSession::switchSource(int id) { m_dataSourceDock->switchSource(id); }
void DataSourceSession::reloadChannels(DataSource *src) { m_dataSourceDock->reloadChannels(src); }
void DataSourceSession::refreshActiveSource() { m_dataSourceDock->refreshActiveSource(); }
void DataSourceSession::activateSource(QWidget *row) { m_dataSourceDock->activateSource(row); }
void DataSourceSession::setSourcePaused(DataSource *src, bool paused) { m_dataSourceDock->setSourcePaused(src, paused); }
void DataSourceSession::exportActiveCsv() { m_dataSourceDock->exportActiveCsv(); }

namespace
{
QString sourceNodeId(int id) { return QStringLiteral("src.%1").arg(id); }
QString channelNodeId(int id, int index) { return QStringLiteral("src.%1.ch.%2").arg(id).arg(index); }
} // namespace

ps::shell::PropertyContent DataSourceSession::sourceContent(int srcId)
{
    ps::shell::PropertyContent content;
    ps::data_source::DataSource *src = sourceById(srcId);
    if (!src)
        return content;
    content.title = QStringLiteral("数据源 · %1").arg(src->config.alias.isEmpty() ? src->config.name : src->config.alias);
    const QString name = src->config.alias.isEmpty() ? src->config.name : src->config.alias;
    int channels = src->isLive()
        ? (src->liveSink ? src->liveSink->channels().size() : 0)
        : src->channels.size();
    qint64 points = 0;
    double duration = 0.0;
    if (src->isLive())
        points = src->liveSink ? src->liveSink->totalSamples() : 0;
    else
        for (const PsChannelInfo *ch : src->channels)
            points += ch->point_count;
    if (!src->channels.isEmpty() && src->channels.first()->point_count > 1)
    {
        const PsChannelInfo *ch = src->channels.first();
        duration = ch->time[ch->point_count - 1] - ch->time[0];
    }
    const QString state = src->row ? src->row->property("srcMeta").toString() : QString();
    const QFileInfo fi(src->config.path);
    QVector<QPair<QString, QString>> rows;
    rows.append({QStringLiteral("类型"), src->isLive() ? QStringLiteral("实时源") : src->config.fmt});
    rows.append({QStringLiteral("别名"), name});
    rows.append({QStringLiteral("状态"), state.isEmpty() ? QStringLiteral("—") : state});
    rows.append({QStringLiteral("位置"), src->config.path.isEmpty() ? src->config.configJson : src->config.path});
    if (src->isLive())
        rows.append({QStringLiteral("扩展"), src->config.extensionId});
    rows.append({QStringLiteral("通道"), QString::number(channels)});
    rows.append({QStringLiteral("参数"), QString::number(src->params.size())});
    rows.append({QStringLiteral("消息"), QString::number(src->msgs.size())});
    rows.append({QStringLiteral("采样点"), QString::number(points)});
    if (duration > 0.0)
        rows.append({QStringLiteral("时长"), QStringLiteral("%1 s").arg(duration, 0, 'f', 2)});
    rows.append({QStringLiteral("时间偏移"), QStringLiteral("%1 s").arg(src->config.offsetSec, 0, 'f', 3)});
    if (fi.exists() && fi.size() > 0)
        rows.append({QStringLiteral("文件大小"),
                     QStringLiteral("%1 MB").arg(fi.size() / 1024.0 / 1024.0, 0, 'f', 2)});
    if (src->isLive())
        rows.append({QStringLiteral("录制"),
                     src->liveSink && src->liveSink->recording()
                         ? QStringLiteral("进行中 · %1").arg(src->liveSink->recordPath())
                         : QStringLiteral("未录制")});
    content.rows = rows;
    content.editable = {QStringLiteral("别名"), QStringLiteral("时间偏移")};
    return content;
}

ps::shell::PropertyContent DataSourceSession::channelContent(int srcId, int index)
{
    ps::shell::PropertyContent content;
    ps::data_source::DataSource *src = sourceById(srcId);
    if (!src || index < 0)
        return content;
    const QVector<const PsChannelInfo *> view =
        src->isLive() ? (src->liveSink ? src->liveSink->channels() : QVector<const PsChannelInfo *>())
                  : src->channels;
    if (index >= view.size())
        return content;
    const PsChannelInfo *ch = view[index];
    if (!ch)
        return content;
    content.title = QStringLiteral("通道 · %1").arg(QString::fromUtf8(ch->name ? ch->name : ""));
    double duration = 0.0;
    if (ch->point_count > 1)
        duration = ch->time[ch->point_count - 1] - ch->time[0];
    QVector<QPair<QString, QString>> rows;
    rows.append({QStringLiteral("来源"), src->config.alias.isEmpty() ? src->config.name : src->config.alias});
    rows.append({QStringLiteral("名称"), QString::fromUtf8(ch->name ? ch->name : "")});
    if (ch->unit && *ch->unit)
        rows.append({QStringLiteral("单位"), QString::fromUtf8(ch->unit)});
    if (ch->dataset && ch->table && ch->column)
    {
        const int32_t vt = ps_dataset_column_type(
            const_cast<PsDataset *>(static_cast<const PsDataset *>(ch->dataset)), ch->table, ch->column);
        if (vt >= 0)
            rows.append({QStringLiteral("类型"), QString::fromUtf8(ps_value_type_name(vt))});
    }
    rows.append({QStringLiteral("采样点"), QString::number(ch->point_count)});
    if (duration > 0.0)
        rows.append({QStringLiteral("时长"), QStringLiteral("%1 s").arg(duration, 0, 'f', 2)});
    rows.append({QStringLiteral("速率"),
                 duration > 0.0
                     ? QStringLiteral("%1 Hz").arg((ch->point_count - 1) / duration, 0, 'f',
                                                   (ch->point_count - 1) / duration >= 10 ? 0 : 1)
                     : QStringLiteral("—")});
    content.rows = rows;
    return content;
}

void DataSourceSession::showSourceProps(QWidget *row)
{
    const int id = sourceIdForRow(row);
    auto &pm = m_mw->properties();
    if (pm.hasNode(sourceNodeId(id)))
        pm.selectNode(sourceNodeId(id));
    else
        pm.showContent(sourceContent(id));
}

void DataSourceSession::applySourceProp(int srcId, const QString &key, const QString &text)
{
    ps::data_source::DataSource *src = sourceById(srcId);
    if (!src)
        return;
    if (key == QStringLiteral("别名"))
    {
        const QString alias = text.trimmed();
        if (alias.isEmpty())
            return;
        src->config.alias = alias;
        for (auto *c : ps::bridge::allCanvases(m_mw))
            ps::bridge::canvasRenameSource(c, src->config.path, alias);
    }
    else if (key == QStringLiteral("时间偏移"))
    {
        bool ok = false;
        const double off = text.split(QLatin1Char(' ')).first().toDouble(&ok);
        if (ok)
            setSourceOffset(src, off, true);
    }
    refreshPropertyNodes();
}

void DataSourceSession::refreshPropertyNodes()
{
    auto &pm = m_mw->properties();
    pm.registerNode(ps::shell::PropertyNode{QStringLiteral("src"), QString(), QStringLiteral("数据源"),
                                            QStringLiteral(":/icons/panel.svg"), {}, {}});
    pm.clearChildren(QStringLiteral("src"));
    for (const ps::data_source::DataSource &src : m_sources)
    {
        const int id = src.config.id;
        const QString name = src.config.alias.isEmpty() ? src.config.name : src.config.alias;
        pm.registerNode(ps::shell::PropertyNode{
            sourceNodeId(id), QStringLiteral("src"), name, QStringLiteral(":/icons/panel.svg"),
            [this, id] { return sourceContent(id); },
            [this, id](const QString &key, const QString &text) { applySourceProp(id, key, text); }});
        const QVector<const PsChannelInfo *> view =
            src.isLive() ? (src.liveSink ? src.liveSink->channels() : QVector<const PsChannelInfo *>())
                     : src.channels;
        for (int i = 0; i < view.size(); ++i)
        {
            const PsChannelInfo *ch = view[i];
            const QString label = QString::fromUtf8(ch && ch->name ? ch->name : "");
            pm.registerNode(ps::shell::PropertyNode{channelNodeId(id, i), sourceNodeId(id), label,
                                                    QStringLiteral(":/icons/panel.svg"),
                                                    [this, id, i] { return channelContent(id, i); }, {}});
        }
    }
}

void DataSourceSession::applySrcFilter()
{
    if (!m_srcFilter || !m_srcTabs)
        return;
    const QString q = m_srcFilter->text().trimmed();
    const int tab = m_srcTabs->currentIndex();
    // 源内联数据列：源始终显示，按组/叶子过滤
    if (QWidget *host = m_srcRows ? m_srcRows->parentWidget() : nullptr)
        for (auto *panel : host->findChildren<QTreeWidget *>())
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

void DataSourceSession::restoreSavedConfigs()
{
    m_savedConfigs.clear();
    const QString raw = SettingsManager::instance().get(ps::settings::kDataSourceConfigs);
    for (const QJsonValue &v : QJsonDocument::fromJson(raw.toUtf8()).array())
    {
        const ps::data_source::DataSourceConfig cfg =
            ps::data_source::DataSourceConfig::fromJson(v.toObject());
        if (cfg.schemaVersion > ps::data_source::DataSourceConfig::kSchemaVersion)
            continue;
        const QString key = cfg.sourceKey.isEmpty() ? cfg.path : cfg.sourceKey;
        if (!key.isEmpty())
            m_savedConfigs.insert(key, cfg);
    }
}

void DataSourceSession::applySavedConfig(ps::data_source::DataSource &src)
{
    const QString key = src.config.sourceKey.isEmpty() ? src.config.path : src.config.sourceKey;
    auto it = m_savedConfigs.constFind(key);
    if (it == m_savedConfigs.constEnd())
        it = m_savedConfigs.constFind(src.config.path);
    if (it == m_savedConfigs.constEnd())
        return;
    const ps::data_source::DataSourceConfig &saved = it.value();
    if (!saved.alias.isEmpty())
        src.config.alias = saved.alias;
    if (!saved.name.isEmpty() && src.config.name.isEmpty())
        src.config.name = saved.name;
    if (saved.offsetSec != 0.0)
    {
        src.config.offsetSec = saved.offsetSec;
        setSourceOffset(&src, saved.offsetSec, true);
    }
}

void DataSourceSession::persistSourceConfigsSoon()
{
    if (m_configTimer)
        m_configTimer->start();
}

void DataSourceSession::persistSourceConfigs()
{
    QJsonArray arr;
    m_savedConfigs.clear();
    for (const ps::data_source::DataSource &src : m_sources)
    {
        arr.append(src.config.toJson());
        const QString key = src.config.sourceKey.isEmpty() ? src.config.path : src.config.sourceKey;
        if (!key.isEmpty())
            m_savedConfigs.insert(key, src.config);
    }
    SettingsManager::instance().set(ps::settings::kDataSourceConfigs,
                                   QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

bool DataSourceSession::loadFile(const QString &path, bool interactive)
{
    const bool ok = m_dataSourceDock->loadFile(path, interactive);
    if (ok)
    {
        for (ps::data_source::DataSource &src : m_sources)
            if (src.config.path == path)
                applySavedConfig(src);
        persistSourceConfigs();
    }
    return ok;
}

void DataSourceSession::connectLiveSource(const QString &extensionId)
{
    m_dataSourceDock->connectLiveSource(extensionId);
    for (ps::data_source::DataSource &src : m_sources)
        if (src.isLive())
            applySavedConfig(src);
}

bool DataSourceSession::openLiveInput(const QStringList &ids, const QString &config, DataSource &dst,
                               QString *fanoutNote, QString *err)
{
    const bool ok = m_dataSourceDock->openLiveInput(ids, config, dst, fanoutNote, err);
    if (ok)
        persistSourceConfigs();
    return ok;
}

void DataSourceSession::configureLiveSource(DataSource *src)
{
    m_dataSourceDock->configureLiveSource(src);
    persistSourceConfigsSoon();
}
void DataSourceSession::pollLiveSources() { m_dataSourceDock->pollLiveSources(); }
void DataSourceSession::refreshLiveSource(DataSource *src)
{
    m_dataSourceDock->refreshLiveSource(src);
    persistSourceConfigsSoon();
}
void DataSourceSession::closeLiveSource(DataSource *src)
{
    m_dataSourceDock->closeLiveSource(src);
    persistSourceConfigsSoon();
}








void DataSourceSession::openPath(const QString &path) { loadFile(path, true); }
void DataSourceSession::connectSource(const QString &id) { connectLiveSource(id); }

bool DataSourceSession::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease && watched->property("srcRow").toBool())
    {
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *row = qobject_cast<QWidget *>(watched);
        activateSource(row);
        if (me->button() == Qt::RightButton)
            showSourceMenu(row, me->globalPosition().toPoint());
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void DataSourceSession::onClose()
{
    persistSourceConfigs();
    for (ps::data_source::DataSource &src : m_sources)
        closeLiveSource(&src);
    if (m_liveTimer)
        m_liveTimer->stop();
    if (SettingsManager::instance().get(ps::settings::kRestoreLayout))
    {
        const QString path = m_mw->session().layoutPath();
        saveLayoutTo(path.isEmpty() ? SessionManager::defaultLayoutPath() : path);
    }
}

void DataSourceSession::saveLayoutTo(const QString &path)
{
    persistSourceConfigs();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("PlotScope");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("geometry")] = QString::fromLatin1(saveGeometry().toBase64());
    root[QStringLiteral("docks")] = QString::fromLatin1(m_mw->docks().saveLayout().toBase64());
    root[QStringLiteral("current")] = m_mw->pages()->currentIndex();

    QJsonArray pages;
    QJsonArray titles;
    const int last = m_mw->pages()->count() - 1;
    for (int i = 0; i < m_mw->pages()->count(); ++i)
    {
        if (i == last && m_mw->pages()->tabText(i) == QStringLiteral("＋"))
            continue;
        pages.append(ps::bridge::pageState(m_mw, m_mw->pages()->widget(i)));
        titles.append(m_mw->pages()->tabText(i));
    }
    root[QStringLiteral("pages")] = pages;
    root[QStringLiteral("titles")] = titles;
    QJsonArray markers;
    for (const auto &mk : m_timeMarkers)
    {
        QJsonObject mo;
        mo[QStringLiteral("id")] = mk.id;
        mo[QStringLiteral("t")] = mk.t;
        mo[QStringLiteral("label")] = mk.label;
        markers.append(mo);
    }
    root[QStringLiteral("markers")] = markers;

    // 数据源与布局一起保存：重启后先重新加载日志，再回填画布通道
    // 数据源与布局一起保存：重启后按顺序重载全部源，再回填画布通道
    QJsonArray sources;
    for (const ps::data_source::DataSource &src : m_sources)
    {
        if (src.config.path.isEmpty() || src.isLive())
            continue;
        QJsonObject so;
        so[QStringLiteral("path")] = src.config.path;
        so[QStringLiteral("name")] = src.config.name;
        so[QStringLiteral("alias")] = src.config.alias;
        so[QStringLiteral("offset")] = src.config.offsetSec;
        so[QStringLiteral("sourceKey")] = src.config.sourceKey;
        so[QStringLiteral("fmt")] = src.config.fmt;
        so[QStringLiteral("extensionId")] = src.config.extensionId;
        so[QStringLiteral("mode")] = int(src.config.mode);
        so[QStringLiteral("configJson")] = src.config.configJson;
        sources.append(so);
    }
    root[QStringLiteral("sources")] = sources;

    QJsonArray transforms;
    for (const ps::data_source::DataSource &src : m_sources)
    {
        for (const ps::data_source::TransformRecipe &r : src.recipes)
        {
            QJsonObject o;
            o[QStringLiteral("source")] = src.config.path;
            o[QStringLiteral("kind")] = r.kind;
            o[QStringLiteral("input")] = r.input;
            o[QStringLiteral("filter")] = r.filter;
            o[QStringLiteral("params")] = r.params;
            o[QStringLiteral("output")] = r.output;
            o[QStringLiteral("degrees")] = r.degrees;
            o[QStringLiteral("unwrap")] = r.unwrap;
            if (r.kind == QLatin1String("quat"))
            {
                QJsonArray q;
                for (const QString &s : r.quat)
                    q.append(s);
                o[QStringLiteral("quat")] = q;
            }
            transforms.append(o);
        }
    }
    root[QStringLiteral("transforms")] = transforms;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        log(QStringLiteral("保存布局失败：%1").arg(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    m_mw->session().setLayoutPath(path);
    log(QStringLiteral("已保存布局：%1").arg(path));
}

void DataSourceSession::importLayoutFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        log(QStringLiteral("导入布局失败：%1").arg(f.errorString()));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("PlotScope"))
    {
        log(QStringLiteral("不是 PlotScope 布局文件"));
        return;
    }

    // 先恢复数据源，buildPage 才能按通道名取到内核指针
    for (const QJsonValue &v : root.value(QStringLiteral("sources")).toArray())
    {
        const QString path = v.toObject().value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QFile::exists(path))
            continue;
        bool loaded = false;
        for (int i = 0; m_srcRows && i < m_srcRows->count(); ++i)
        {
            QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
            if (row && row->property("srcPath").toString() == path)
            {
                loaded = true;
                break;
            }
        }
        if (!loaded)
            loadFile(path, false);
        const QString alias = v.toObject().value(QStringLiteral("alias")).toString();
        const QJsonObject saved = v.toObject();
        for (ps::data_source::DataSource &src : m_sources)
        {
            if (src.config.path != path)
                continue;
            if (!alias.isEmpty())
                src.config.alias = alias;
            src.config.sourceKey = saved.value(QStringLiteral("sourceKey")).toString(src.config.sourceKey);
            src.config.fmt = saved.value(QStringLiteral("fmt")).toString(src.config.fmt);
            src.config.extensionId = saved.value(QStringLiteral("extensionId")).toString(src.config.extensionId);
            src.config.configJson = saved.value(QStringLiteral("configJson")).toString(src.config.configJson);
        }
        const double offset = v.toObject().value(QStringLiteral("offset")).toDouble(0.0);
        for (ps::data_source::DataSource &src : m_sources)
            if (src.config.path == path)
                setSourceOffset(&src, offset, false);
    }

    // 重放变换配方；派生通道先写回，buildPage 才能按名字找到
    for (const QJsonValue &v : root.value(QStringLiteral("transforms")).toArray())
    {
        const QJsonObject o = v.toObject();
        ps::data_source::DataSource *src = nullptr;
        for (ps::data_source::DataSource &s : m_sources)
            if (s.config.path == o.value(QStringLiteral("source")).toString())
                src = &s;
        if (!src || !src->ds)
            continue;
        TransformRecipe r;
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.input = o.value(QStringLiteral("input")).toString();
        r.filter = o.value(QStringLiteral("filter")).toString();
        r.params = o.value(QStringLiteral("params")).toObject();
        r.output = o.value(QStringLiteral("output")).toString();
        r.degrees = o.value(QStringLiteral("degrees")).toBool(true);
        r.unwrap = o.value(QStringLiteral("unwrap")).toBool(true);
        const QJsonArray q = o.value(QStringLiteral("quat")).toArray();
        for (int i = 0; i < 4 && i < q.size(); ++i)
            r.quat[i] = q.at(i).toString();
        const int derivedCap = SettingsManager::instance().get(ps::settings::kDerivedMaxPoints);
        if (derivedCap > 0)
        {
            QStringList ins = r.kind == QLatin1String("quat")
                                  ? QStringList{r.quat[0], r.quat[1], r.quat[2], r.quat[3]}
                                  : QStringList{r.input};
            uint32_t worst = 0;
            for (const QString &name : ins)
            {
                const uint32_t count = ps_dataset_channel_count(src->ds);
                for (uint32_t i = 0; i < count; ++i)
                {
                    const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
                    if (ch && name == QString::fromUtf8(ch->name))
                    {
                        worst = qMax(worst, ch->point_count);
                        break;
                    }
                }
            }
            if (worst > static_cast<uint32_t>(derivedCap))
            {
                log(QStringLiteral("重放变换跳过：%1（%2 点 > 上限 %3）")
                        .arg(r.output).arg(worst).arg(derivedCap));
                continue;
            }
        }
        char err[512] = {0};
        int rc = 0;
        if (r.kind == QLatin1String("quat"))
            rc = ps_dataset_apply_quat_rpy(src->ds, r.quat[0].toUtf8().constData(), r.quat[1].toUtf8().constData(),
                                           r.quat[2].toUtf8().constData(), r.quat[3].toUtf8().constData(),
                                           r.output.toUtf8().constData(), r.degrees ? 1 : 0, r.unwrap ? 1 : 0,
                                           err, sizeof err);
        else
            rc = ps_dataset_apply_filter(
                src->ds, r.input.toUtf8().constData(), r.filter.toUtf8().constData(),
                QJsonDocument(r.params).toJson(QJsonDocument::Compact).constData(),
                r.output.toUtf8().constData(), err, sizeof err);
        if (rc != 0)
        {
            log(QStringLiteral("重放变换失败：%1（%2）").arg(r.output, QString::fromUtf8(err)));
            continue;
        }
        src->recipes.append(r);
        const QStringList names = r.kind == QLatin1String("quat")
                                      ? QStringList{r.output + QStringLiteral(".roll"),
                                                    r.output + QStringLiteral(".pitch"),
                                                    r.output + QStringLiteral(".yaw")}
                                      : QStringList{r.output};
        for (const QString &n : names)
            if (!src->derived.contains(n))
                src->derived.append(n);
        reloadChannels(src);
    }
    if (!m_sources.empty())
        refreshActiveSource();

    // 清空页签时屏蔽 currentChanged：末尾的 ＋ 触发 newPage 会和删除循环互踢
    m_mw->pages()->blockSignals(true);
    while (m_mw->pages()->count() > 0)
        m_mw->pages()->removeTab(0);
    m_mw->pages()->blockSignals(false);

    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    const QJsonArray titles = root.value(QStringLiteral("titles")).toArray();
    for (int i = 0; i < pages.size(); ++i)
        m_mw->pages()->addTab(ps::bridge::buildPage(m_mw, pages[i].toObject()),
                        titles.at(i).toString(QStringLiteral("页 %1").arg(i + 1)));

    const int plusIdx = m_mw->pages()->count();
    m_mw->pages()->addTab(new QWidget(m_mw->pages()), QStringLiteral("＋"));
    m_mw->pages()->tabBar()->setTabToolTip(plusIdx, QStringLiteral("新建页"));
    m_mw->pages()->tabBar()->setTabButton(plusIdx, QTabBar::RightSide, nullptr);

    const QByteArray geo = QByteArray::fromBase64(root.value(QStringLiteral("geometry")).toString().toLatin1());
    if (!geo.isEmpty())
        restoreGeometry(geo);
    const QByteArray docks = QByteArray::fromBase64(root.value(QStringLiteral("docks")).toString().toLatin1());
    if (!docks.isEmpty())
    {
        m_mw->docks().restoreLayout(docks);
        // 旧布局常把日志区拉到 150+，空白挤占画布；恢复后统一收到 96。
        if (auto *logDock = findChild<QDockWidget *>(QStringLiteral("dock.log")))
            m_mw->resizeDocks({logDock}, {96}, Qt::Vertical);
    }

    if (m_activeSource >= 0)
        switchSource(m_activeSource);

    m_timeMarkers.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("markers")).toArray())
    {
        const QJsonObject mo = v.toObject();
        QString id = mo.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            id = QStringLiteral("bm-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_timeMarkers.append({id, mo.value(QStringLiteral("t")).toDouble(),
                              mo.value(QStringLiteral("label")).toString()});
    }
    std::sort(m_timeMarkers.begin(), m_timeMarkers.end(),
              [](const ps::TimeBookmark &a, const ps::TimeBookmark &b) { return a.t < b.t; });
    ps::bridge::applyTimeMarkers(m_mw);

    const int cur = root.value(QStringLiteral("current")).toInt(0);
    if (cur >= 0 && cur < m_mw->pages()->count() - 1)
        m_mw->pages()->setCurrentIndex(cur);

    m_mw->session().setLayoutPath(path);
    log(QStringLiteral("已导入布局：%1").arg(path));
}

void DataSourceSession::clearParseCache() { DatasetCache::clear(); }




void DataSourceSession::addTimeMarker(double t, const QString &label, bool prompt)
{
    QString text = label.trimmed();
    if (prompt)
    {
        bool ok = false;
        QString def = text.isEmpty() ? QStringLiteral("书签") : text;
        text = QInputDialog::getText(this, QStringLiteral("添加时间书签"),
                                     QStringLiteral("时间 %1 s\n书签").arg(t, 0, 'f', 2),
                                     QLineEdit::Normal, def, &ok);
        if (!ok)
            return;
        text = text.trimmed();
        if (text.isEmpty())
            text = def;
    }
    if (text.isEmpty())
        text = QStringLiteral("%1 s").arg(t, 0, 'f', 2);
    for (auto &mk : m_timeMarkers)
        if (qAbs(mk.t - t) < 1e-4)
        {
            mk.label = text;
            ps::bridge::applyTimeMarkers(m_mw);
            m_mw->logs().info(QStringLiteral("更新时间书签：%1 @ %2 s").arg(text).arg(t, 0, 'f', 3));
            return;
        }
    m_timeMarkers.append({QStringLiteral("bm-") + QUuid::createUuid().toString(QUuid::WithoutBraces),
                          t, text});
    std::sort(m_timeMarkers.begin(), m_timeMarkers.end(),
              [](const ps::TimeBookmark &a, const ps::TimeBookmark &b) { return a.t < b.t; });
    ps::bridge::applyTimeMarkers(m_mw);
    log(QStringLiteral("添加时间书签：%1 @ %2 s").arg(text).arg(t, 0, 'f', 3));
}

void DataSourceSession::removeTimeMarker(const QString &id)
{
    for (int i = 0; i < m_timeMarkers.size(); ++i)
    {
        if (m_timeMarkers.at(i).id != id)
            continue;
        const QString label = m_timeMarkers.at(i).label;
        m_timeMarkers.removeAt(i);
        ps::bridge::applyTimeMarkers(m_mw);
        m_mw->logs().info(QStringLiteral("删除时间书签：%1").arg(label));
        return;
    }
}

void DataSourceSession::renameTimeMarker(const QString &id, const QString &label)
{
    const QString text = label.trimmed();
    if (text.isEmpty())
        return;
    for (auto &mk : m_timeMarkers)
    {
        if (mk.id != id)
            continue;
        mk.label = text;
        ps::bridge::applyTimeMarkers(m_mw);
        m_mw->logs().info(QStringLiteral("重命名时间书签：%1").arg(text));
        return;
    }
}

void DataSourceSession::moveTimeMarker(const QString &id, double to)
{
    int index = -1;
    for (int i = 0; i < m_timeMarkers.size(); ++i)
        if (m_timeMarkers.at(i).id == id)
        {
            index = i;
            break;
        }
    if (index < 0 || qAbs(to - m_timeMarkers.at(index).t) < 1e-4)
        return;
    const QString label = m_timeMarkers.at(index).label;
    m_timeMarkers[index].t = to;
    std::sort(m_timeMarkers.begin(), m_timeMarkers.end(),
              [](const ps::TimeBookmark &a, const ps::TimeBookmark &b) { return a.t < b.t; });
    ps::bridge::applyTimeMarkers(m_mw);
    m_mw->logs().info(QStringLiteral("移动时间书签：%1 @ %2 s").arg(label).arg(to, 0, 'f', 3));
}
