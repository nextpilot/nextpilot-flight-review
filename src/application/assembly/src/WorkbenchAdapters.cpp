#include "WorkbenchAdapters.h"

namespace nextpilot::plotscope::assembly
{

// ==================== DatasetAccessAdapter ====================
DatasetAccessAdapter::DatasetAccessAdapter(ds::DatasetHub &hub, QObject *parent)
    : QObject(parent), m_hub(hub)
{
}

const ds::Dataset *DatasetAccessAdapter::get(ds::DatasetId id) const { return m_hub.get(id); }
QVector<ds::DatasetId> DatasetAccessAdapter::ids() const { return m_hub.ids(); }
int DatasetAccessAdapter::count() const { return m_hub.count(); }
ds::Dataset *DatasetAccessAdapter::getMutable(ds::DatasetId id) { return m_hub.getMutable(id); }
bool DatasetAccessAdapter::remove(ds::DatasetId id) { return m_hub.remove(id); }

wb::Subscription DatasetAccessAdapter::onDidAddDocument(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::DatasetHub::datasetAdded, this,
                     [l](ds::DatasetId id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

wb::Subscription DatasetAccessAdapter::onDidRemoveDocument(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::DatasetHub::datasetRemoved, this,
                     [l](ds::DatasetId id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

// ==================== SourceAccessAdapter ====================
SourceAccessAdapter::SourceAccessAdapter(ds::SourceHub &hub, QObject *parent)
    : QObject(parent), m_hub(hub)
{
}

const ds::DataSourceInfo *SourceAccessAdapter::get(int id) const { return m_hub.get(id); }
int SourceAccessAdapter::sourceOfDataset(int datasetId) const { return m_hub.sourceOfDataset(datasetId); }
QVector<int> SourceAccessAdapter::ids() const { return m_hub.ids(); }
bool SourceAccessAdapter::remove(int id, QVector<int> *datasetIds) { return m_hub.remove(id, datasetIds); }
int SourceAccessAdapter::sourceByOrigin(const QString &origin) const { return m_hub.sourceByOrigin(origin); }
bool SourceAccessAdapter::setState(int id, ds::SourceState state, const QString &error)
{
    return m_hub.setState(id, state, error);
}
void SourceAccessAdapter::releaseOrigin(const QString &origin) { m_hub.releaseOrigin(origin); }
bool SourceAccessAdapter::addDataset(int id, int datasetId) { return m_hub.addDataset(id, datasetId); }
int SourceAccessAdapter::add(ds::DataSourceInfo info) { return m_hub.add(info); }
bool SourceAccessAdapter::update(int id, const ds::DataSourceInfo &info) { return m_hub.update(id, info); }
void SourceAccessAdapter::demandOrigin(const QString &origin) { m_hub.demandOrigin(origin); }
int SourceAccessAdapter::count() const { return m_hub.count(); }

wb::Subscription SourceAccessAdapter::onDidAddSource(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::SourceHub::sourceAdded, this,
                     [l](int id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

wb::Subscription SourceAccessAdapter::onDidChangeSource(std::function<void(const QString &id)> l)
{
    auto c = connect(&m_hub, &ds::SourceHub::sourceChanged, this,
                     [l](int id) { l(QString::number(id)); });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

// ==================== IngestAdapter ====================
IngestAdapter::IngestAdapter(ctx::AppContext &appCtx, QObject *parent)
    : QObject(parent), m_appCtx(appCtx), m_pipe(appCtx.ingest())
{
}

foundation::Result<int> IngestAdapter::bindParser(int sourceId, const ds::ParserBinding &binding,
                                                   ds::DatasetId preferred)
{
    return m_pipe.bindParser(sourceId, binding, preferred);
}

qint64 IngestAdapter::feed(int sourceId, const uint8_t *data, qint64 size)
{
    return m_pipe.feed(sourceId, data, size);
}

void IngestAdapter::finish(int sourceId) { m_pipe.finish(sourceId); }

QVector<ds::IngestChannel> IngestAdapter::channels(int sourceId) const
{
    QVector<ds::IngestChannel> out;
    for (const auto &pc : m_pipe.channels(sourceId))
    {
        ds::IngestChannel c;
        c.parserId = pc.parserId;
        c.topicFilter = pc.topicFilter;
        c.vtable = pc.vtable;
        c.ctx = pc.ctx;
        c.datasetId = pc.datasetId;
        c.active = pc.active;
        c.error = pc.error;
        out.append(c);
    }
    return out;
}

wb::Subscription IngestAdapter::onDidStartLoad(std::function<void()> l)
{
    // 加载生命周期在 AppContext 上（DataLoadHandler 进入摄取前发）。装配层桥接，
    // 模块只见 IIngest，不碰宿主（评论 52436915 裁定）。
    auto c = connect(&m_appCtx, &ctx::AppContext::dataLoadStarted, this, [l]() { l(); });
    return c;
}

wb::Subscription IngestAdapter::onDidFinishLoad(std::function<void(bool ok)> l)
{
    auto c = connect(&m_appCtx, &ctx::AppContext::dataLoadFinished, this,
                     [l](bool ok) { l(ok); });
    return c;
}

wb::Subscription IngestAdapter::onDidReportProgress(std::function<void(int, const QString &)> l)
{
    auto c = connect(&m_pipe, &ctx::IngestPipeline::progressChanged, this,
                     [l](int, uint64_t done, uint64_t total) {
                         // 文案与 baseline DataModule 状态栏一致：total>0 百分比，否则已读字节数。
                         const int pct = total > 0 ? static_cast<int>((done * 100) / total) : 0;
                         const QString text = total > 0
                                                  ? QStringLiteral("加载 %1%").arg(pct)
                                                  : QStringLiteral("加载 %1 字节").arg(done);
                         l(pct, text);
                     });
    return c; // Subscription = QMetaObject::Connection，调用方 disconnect(c) 退订
}

// ==================== PlaybackAdapter ====================
bool PlaybackAdapter::load(const QJsonObject &jobJson, foundation::Result<void> *outError)
{
    foundation::Result<void> localErr;
    m_job = intent::Job::fromJson(jobJson, outError ? outError : &localErr);
    m_loaded = (outError ? outError->ok() : localErr.ok());
    return m_loaded;
}

const intent::Intent *PlaybackAdapter::at(int i) const { return &m_job.at(i); }

void PlaybackAdapter::clear()
{
    // Job 无 clear；用空 Job 重建。
    m_job = intent::Job();
    m_loaded = false;
}

// ==================== CommandsAdapter ====================
foundation::Result<intent::IntentResult> CommandsAdapter::Bridge::execute(const intent::Intent &in)
{
    foundation::Result<QVariant> r = handler(in.params);
    if (!r.ok())
        return foundation::Result<intent::IntentResult>(r.error());
    intent::IntentResult out;
    out.payload = r.value();
    return out;
}

CommandsAdapter::CommandsAdapter(
    std::function<void(const QString &, intent::IIntentHandler *)> sink, QObject *parent)
    : QObject(parent), m_sink(std::move(sink))
{
}

void CommandsAdapter::registerCommand(const QString &id, Handler h)
{
    if (id.isEmpty() || !h || m_commands.contains(id))
        return;

    // 适配器：把 Handler 包装成 IIntentHandler，注册进 RuntimeHost 现有池。
    // commandId == Intent.op、args == Intent.params；执行链仍只有 RuntimeHost::execute。
    auto *bridge = new Bridge();
    bridge->id = id;
    bridge->handler = std::move(h);
    if (m_sink)
        m_sink(id, bridge);
    m_commands.insert(id, bridge);
}

foundation::Result<QVariant> CommandsAdapter::executeCommand(const QString &id,
                                                              const QVariantMap &args)
{
    auto it = m_commands.constFind(id);
    if (it == m_commands.end() || !(*it) || !(*it)->active)
        return foundation::fail(QStringLiteral("E_NO_COMMAND"),
                                QStringLiteral("未注册命令: %1").arg(id));

    intent::Intent in;
    in.op = id;
    in.params = args;
    foundation::Result<intent::IntentResult> r = (*it)->execute(in);
    if (!r.ok())
        return foundation::Result<QVariant>(r.error());
    return foundation::Result<QVariant>(r.value().payload);
}

void CommandsAdapter::unregisterCommand(const QString &id)
{
    auto it = m_commands.find(id);
    if (it == m_commands.end())
        return;
    // 不 delete：bridge 已注册进 RuntimeHost 池，销毁时机由 QObject 父子树统一管理。
    // 置 active=false 后 canHandle 永不命中，功能上等价于注销。
    if (*it)
        (*it)->active = false;
    m_commands.erase(it);
}

} // namespace nextpilot::plotscope::assembly
