#pragma once

// A1b 装配层适配器：把 libs 具体 Hub 包成 workbench 的强类型接口。
// 只有本文件（装配层）允许 include 具体 Hub；模块侧只见 workbench 接口。
//
// 每个 adapter 只持有被引对象的引用（不拥有），生命周期归 AppContext/RuntimeHost。

#include "IDatasetAccess.h"
#include "ISourceAccess.h"
#include "IIngest.h"
#include "IRecorder.h"
#include "IPlayback.h"
#include "IWorkspace.h"
#include "ICommands.h"
#include "IScriptCapabilities.h"
#include "IPanelsService.h"
#include "IFormatHints.h"
#include "IThemeService.h"
#include "ISlotsService.h"
#include "IContributionsInstaller.h"
#include "IViewsService.h"

#include "DatasetHub.h"
#include "SourceHub.h"
#include "IngestPipeline.h"
#include "JobRecorder.h"
#include "SettingsStore.h"
#include "DerivedSeriesHub.h"
#include "InteractionHub.h"
#include "AppContext.h"
#include "Intent.h"
#include "ToolboxHub.h"
#include "UiSlotHub.h"
#include "ViewRegistry.h"
#include "RuntimeHost.h" // ThemeServiceAdapter 内联构造读 uiSlots()，需完整类型

#include <QObject>
#include <QVariantMap>
#include <algorithm>

namespace nextpilot::plotscope::host
{
class RuntimeHost; // ThemeServiceAdapter 引用成员；完整定义由包含方（WorkbenchServices.h）保证
}

namespace nextpilot::plotscope::assembly {
namespace ds = nextpilot::plotscope::dataset;
namespace ctx = nextpilot::plotscope::context;
namespace intent = nextpilot::plotscope::intent;
namespace rec = nextpilot::plotscope::recording;
namespace ui = nextpilot::plotscope::uischema;
namespace wb = nextpilot::plotscope::workbench;
namespace registry = nextpilot::plotscope::registry;
namespace foundation = nextpilot::plotscope::foundation;
namespace host = nextpilot::plotscope::host;

// ---- IDatasetAccess ----
class DatasetAccessAdapter : public QObject, public ds::IDatasetAccess
{
    Q_OBJECT
public:
    explicit DatasetAccessAdapter(ds::DatasetHub &hub, QObject *parent = nullptr);

    const ds::Dataset *get(ds::DatasetId id) const override;
    QVector<ds::DatasetId> ids() const override;
    int count() const override;
    ds::Dataset *getMutable(ds::DatasetId id) override;
    bool remove(ds::DatasetId id) override;

    wb::Subscription onDidAddDocument(std::function<void(const QString &id)> l) override;
    wb::Subscription onDidRemoveDocument(std::function<void(const QString &id)> l) override;

private:
    ds::DatasetHub &m_hub;
};

// ---- ISourceAccess ----
class SourceAccessAdapter : public QObject, public ds::ISourceAccess
{
    Q_OBJECT
public:
    explicit SourceAccessAdapter(ds::SourceHub &hub, QObject *parent = nullptr);

    const ds::DataSourceInfo *get(int id) const override;
    int sourceOfDataset(int datasetId) const override;
    QVector<int> ids() const override;
    bool remove(int id, QVector<int> *datasetIds = nullptr) override;
    int sourceByOrigin(const QString &origin) const override;
    bool setState(int id, ds::SourceState state, const QString &error = QString()) override;
    void releaseOrigin(const QString &origin) override;
    bool addDataset(int id, int datasetId) override;
    int add(ds::DataSourceInfo info) override;
    bool update(int id, const ds::DataSourceInfo &info) override;
    void demandOrigin(const QString &origin) override;
    int count() const override;

    wb::Subscription onDidAddSource(std::function<void(const QString &id)> l) override;
    wb::Subscription onDidChangeSource(std::function<void(const QString &id)> l) override;

private:
    ds::SourceHub &m_hub;
};

// ---- IIngest ----
class IngestAdapter : public QObject, public ds::IIngest
{
    Q_OBJECT
public:
    // 持 AppContext&：管道方法转发到 appCtx.ingest()，加载生命周期转发其信号（评论 52436915）。
    explicit IngestAdapter(ctx::AppContext &appCtx, QObject *parent = nullptr);

    foundation::Result<int> bindParser(int sourceId, const ds::ParserBinding &binding,
                                       ds::DatasetId preferredDatasetId = ds::kInvalidDataset) override;
    qint64 feed(int sourceId, const uint8_t *data, qint64 size) override;
    void finish(int sourceId) override;
    QVector<ds::IngestChannel> channels(int sourceId) const override;

    wb::Subscription onDidStartLoad(std::function<void()> l) override;
    wb::Subscription onDidFinishLoad(std::function<void(bool ok)> l) override;
    wb::Subscription onDidReportProgress(std::function<void(int, const QString &)> l) override;

private:
    ctx::AppContext &m_appCtx;
    ctx::IngestPipeline &m_pipe;
};

// ---- IRecorder / IPlayback ----
class RecorderAdapter : public rec::IRecorder
{
public:
    explicit RecorderAdapter(intent::JobRecorder &rec) : m_rec(rec) {}
    void begin() override { m_rec.begin(); }
    void record(const intent::Intent &i) override { m_rec.record(i); }
    bool isRecording() const override { return m_rec.isRecording(); }
    void setRecording(bool on) override { m_rec.setRecording(on); }
    int size() const override { return m_rec.size(); }
    void clear() override { m_rec.clear(); }

private:
    intent::JobRecorder &m_rec;
};

class PlaybackAdapter : public rec::IPlayback
{
public:
    bool load(const QJsonObject &jobJson, foundation::Result<void> *outError = nullptr) override;
    bool isLoaded() const override { return m_loaded; }
    int size() const override { return m_job.size(); }
    const intent::Intent *at(int i) const override;
    void clear() override;

private:
    intent::Job m_job;
    bool m_loaded = false;
};

// ---- IWorkspace ----
class WorkspaceAdapter : public wb::IWorkspace
{
public:
    WorkspaceAdapter(ds::IDatasetAccess &datasets, ds::ISourceAccess &sources, ds::IIngest &ingest,
                     rec::IRecorder &recorder, rec::IPlayback &playback, ctx::SettingsStore &settings,
                     ctx::DerivedSeriesHub &derived, std::function<QVector<QPair<QString, QString>>()> driverMetas = {})
        : m_datasets(datasets), m_sources(sources), m_ingest(ingest), m_recorder(recorder), m_playback(playback),
          m_settings(settings), m_derived(derived), m_driverMetas(std::move(driverMetas))
    {
    }

    ds::IDatasetAccess &dataset() override { return m_datasets; }
    ds::ISourceAccess &sources() override { return m_sources; }
    ds::IIngest &ingest() override { return m_ingest; }
    rec::IRecorder &recorder() override { return m_recorder; }
    rec::IPlayback &playback() override { return m_playback; }
    QVariant getConfiguration(const QString &key, const QVariant &def = {}) const override
    {
        return m_settings.value(key, def);
    }
    void setConfiguration(const QString &key, const QVariant &value) override { m_settings.setValue(key, value); }
    bool flushConfiguration(QString *err = nullptr) override { return m_settings.flush(err); }
    void removeConfiguration(const QString &key) override { m_settings.remove(key); }
    QStringList configurationKeys(const QString &prefix = {}) const override
    {
        const QStringList all = m_settings.keys();
        if (prefix.isEmpty())
            return all;
        QStringList out;
        for (const QString &k : all)
            if (k.startsWith(prefix))
                out.append(k);
        return out;
    }
    QVector<QPair<QString, QString>> sourceDriverMetas() const override
    {
        return m_driverMetas ? m_driverMetas() : QVector<QPair<QString, QString>>{};
    }
    ctx::DerivedSeriesHub &derived() override { return m_derived; }

private:
    ds::IDatasetAccess &m_datasets;
    ds::ISourceAccess &m_sources;
    ds::IIngest &m_ingest;
    rec::IRecorder &m_recorder;
    rec::IPlayback &m_playback;
    ctx::SettingsStore &m_settings;
    ctx::DerivedSeriesHub &m_derived;
    std::function<QVector<QPair<QString, QString>>()> m_driverMetas;
};

// ---- ICommands ----
// 实现 ICommands：registerCommand 把 Handler 适配成 IIntentHandler 注入 RuntimeHost 现有池
// （commandId == Intent.op、args == Intent.params），执行链只有 RuntimeHost::execute 一条。
class CommandsAdapter : public QObject, public wb::ICommands
{
    Q_OBJECT
public:
    // sink：把内部生成的 IIntentHandler 注册进 RuntimeHost 池。
    explicit CommandsAdapter(std::function<void(const QString &, intent::IIntentHandler *)> sink,
                             QObject *parent = nullptr);

    void registerCommand(const QString &id, Handler h) override;
    foundation::Result<QVariant> executeCommand(const QString &id, const QVariantMap &args) override;
    void unregisterCommand(const QString &id) override;

private:
    // 把 Handler 适配成 IIntentHandler 注入 RuntimeHost 池；active 支持注销后置哑。
    struct Bridge : intent::IIntentHandler
    {
        QString id;
        Handler handler;
        bool active = true;
        bool canHandle(const intent::Intent &in) const override { return active && in.op == id; }
        foundation::Result<intent::IntentResult> execute(const intent::Intent &in) override;
    };

    std::function<void(const QString &, intent::IIntentHandler *)> m_sink;
    QHash<QString, Bridge *> m_commands;
};

// ---- IScriptCapabilities ----
// §1.2.1：把 InteractionHub 的脚本能力面抽成接口，纯转发到 hub / AppContext。
// 不新增逻辑；transforms() 从 AppContext 取（InteractionHub 不持有 TransformHub）。
class ScriptCapabilitiesAdapter : public ui::IScriptCapabilities
{
public:
    ScriptCapabilitiesAdapter(ctx::InteractionHub &hub, ctx::AppContext &appCtx, intent::IIntentExecutor &executor,
                              intent::JobRecorder *recorder, std::function<QStringList()> opsProvider = {})
        : m_hub(hub), m_appCtx(appCtx), m_executor(executor), m_recorder(recorder),
          m_opsProvider(std::move(opsProvider))
    {
    }

    void registerTransformEvaluator(TransformEvalFn fn) override { m_hub.registerTransformEvaluator(std::move(fn)); }
    bool hasTransformEvaluator() const override { return m_hub.hasTransformEvaluator(); }
    bool evalTransform(const QJsonObject &req, uint32_t cap, const QVector<double> &t, const QVector<double> &v,
                       foundation::Result<QVector<double>> &out) const override
    {
        return m_hub.evalTransform(req, cap, t, v, out);
    }

    void registerFilterOps(const ui::FilterOps &ops) override { m_hub.registerFilterOps(ops); }
    bool hasFilterOps() const override { return m_hub.hasFilterOps(); }
    const ui::FilterOps &filterOps() const override { return m_hub.filterOps(); }

    void registerMarkerOps(const ui::MarkerOps &ops) override { m_hub.registerMarkerOps(ops); }
    bool hasMarkerOps() const override { return m_hub.hasMarkerOps(); }
    const ui::MarkerOps &markerOps() const override { return m_hub.markerOps(); }

    void registerTransformScriptOps(const ui::TransformScriptOps &ops) override
    {
        m_hub.registerTransformScriptOps(ops);
    }
    bool hasTransformScriptOps() const override { return m_hub.hasTransformScriptOps(); }
    const ui::TransformScriptOps &transformScriptOps() const override { return m_hub.transformScriptOps(); }

    void registerOpCatalogue(OpCatalogueFn fn) override
    {
        // 宿主 registeredOps 是执行器侧的实时 op 清单；模块经本接口注册，
        // adapter 用宿主提供的 provider（若注入）覆盖，保证与实际执行池一致。
        m_hub.registerOpCatalogue(m_opsProvider ? m_opsProvider : std::move(fn));
    }
    QStringList opCatalogue() const override { return m_hub.opCatalogue(); }
    ui::TransformHub &transforms() override { return m_appCtx.transforms(); }

    void registerViewAction(const QString &id, ViewActionHandler handler) override
    {
        m_hub.registerViewAction(id, std::move(handler));
    }
    bool dispatchViewAction(const QString &id, const QVariantMap &params,
                            foundation::Result<QVariant> &out) const override
    {
        return m_hub.dispatchViewAction(id, params, out);
    }
    void appendMessage(const QString &level, const QString &source, const QString &text,
                       const QString &locator = QString()) override
    {
        m_appCtx.appendMessage(level, source, text, locator);
    }
    intent::IIntentExecutor &executor() override { return m_executor; }
    intent::JobRecorder *recorder() override { return m_recorder; }
    ds::DatasetHub &dataHub() override { return m_appCtx.data(); }
    ds::SourceHub &sourcesHub() override { return m_appCtx.sources(); }
    ctx::InteractionHub &interactionsHub() override { return m_hub; }

private:
    ctx::InteractionHub &m_hub;
    ctx::AppContext &m_appCtx;
    intent::IIntentExecutor &m_executor;
    intent::JobRecorder *m_recorder;
    std::function<QStringList()> m_opsProvider;
};

// ---- IPanelsService（A8b-M2 裁定 52441633 §三）：壳读回包面板声明/标题动作。
// 纯接口第一基类（无 QObject），registerSingleton/getService 的 static_cast 恒安全。
class PanelsServiceAdapter : public wb::IPanelsService
{
public:
    explicit PanelsServiceAdapter(ctx::AppContext &appCtx) : m_appCtx(appCtx) {}

    const registry::PanelSpec *panelSpec(const QString &panelType) const override
    {
        return m_appCtx.toolbox().panels.find(panelType);
    }
    QList<registry::PanelSpec> panels() const override
    {
        return m_appCtx.toolbox().panels.values();
    }
    QList<registry::ViewTitleActionSpec> titleActions(const QString &viewId) const override
    {
        return m_appCtx.toolbox().titleActionsFor(viewId);
    }

private:
    ctx::AppContext &m_appCtx;
};

// ---- IFormatHints（同裁定）：壳弹"按格式打开"文件对话框的显示名/过滤串。
class FormatHintsAdapter : public wb::IFormatHints
{
public:
    explicit FormatHintsAdapter(ctx::AppContext &appCtx) : m_appCtx(appCtx) {}

    QString fileFilter(const QString &formatId) const override
    {
        auto *driver = m_appCtx.toolbox().driverForFormat(formatId);
        if (!driver)
            return QString();
        const QStringList suf = driver->suffixes();
        if (suf.isEmpty())
            return QString();
        QStringList patterns;
        for (const QString &s : suf)
            patterns << QStringLiteral("*.") + s;
        return driver->displayName() + QStringLiteral(" (") + patterns.join(QLatin1Char(' '))
               + QLatin1Char(')');
    }
    QString displayName(const QString &formatId) const override
    {
        auto *driver = m_appCtx.toolbox().driverForFormat(formatId);
        return driver ? driver->displayName() : QString();
    }

private:
    ctx::AppContext &m_appCtx;
};

// ---- IThemeService（同裁定）：扩展 token 全集 = 包 token + 模块 token（兜底），
// 按 id 去重（先到者优先）、按 id 升序——语义与壳原合并逻辑一致。
class ThemeServiceAdapter : public wb::IThemeService
{
public:
    ThemeServiceAdapter(ctx::AppContext &appCtx, host::RuntimeHost &hostRef)
        : m_appCtx(appCtx), m_hostRef(hostRef)
    {
    }

    QList<registry::ThemeTokenSpec> tokens() const override
    {
        QList<registry::ThemeTokenSpec> out = m_appCtx.toolbox().themeTokensAll();
        const QList<registry::ThemeTokenSpec> moduleTokens =
            m_hostRef.uiSlots().moduleThemeTokens();
        for (const registry::ThemeTokenSpec &t : moduleTokens)
        {
            bool dup = false;
            for (const registry::ThemeTokenSpec &e : out)
                if (e.id == t.id)
                { dup = true; break; }
            if (!dup)
                out.append(t);
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const registry::ThemeTokenSpec &a, const registry::ThemeTokenSpec &b) {
                             return a.id < b.id;
                         });
        return out;
    }

private:
    ctx::AppContext &m_appCtx;
    host::RuntimeHost &m_hostRef;
};

// ---- ISlotsService（裁定 52442006 Step 1/2）：壳对槽位表读写的唯一通道，
// 每个方法一行转发 UiSlotHub，不加语义。纯接口第一基类，static_cast 恒安全。
class SlotsServiceAdapter final : public wb::ISlotsService
{
public:
    explicit SlotsServiceAdapter(registry::UiSlotHub &slotSvc) : m_slots(slotSvc) {}

    void declareSlot(const QString &area, const QString &label, const QString &owner,
                     registry::SlotKind kind = registry::SlotKind::RibbonGroup,
                     const QString &parentSlotId = QString(), int order = 0,
                     const QString &when = QString()) override
    {
        m_slots.declareSlot(area, label, owner, kind, parentSlotId, order, when);
    }
    void registerActivity(registry::ActivitySpec activity) override
    {
        m_slots.registerActivity(std::move(activity));
    }
    void bindLocal(const QString &area, const registry::UiAction &action,
                   const QString &provider) override
    {
        m_slots.bindLocal(area, action, provider);
    }
    void bindActionProvider(const QString &area, const QString &provider,
                            std::function<QList<registry::UiAction>()> fn) override
    {
        m_slots.bindActionProvider(area, provider, std::move(fn));
    }
    void registerModuleThemeToken(const registry::ThemeTokenSpec &token) override
    {
        m_slots.registerModuleThemeToken(token);
    }
    QList<registry::ThemeTokenSpec> moduleThemeTokens() const override
    {
        return m_slots.moduleThemeTokens();
    }
    QList<registry::ActivitySpec> activities() const override { return m_slots.activities(); }
    const registry::ActivitySpec *activity(const QString &id) const override
    {
        return m_slots.activity(id);
    }
    QList<registry::UiSlot> slotsOfKind(registry::SlotKind kind) const override
    {
        return m_slots.slotsOfKind(kind);
    }
    QList<registry::UiSlot> childrenOf(const QString &parentSlotId) const override
    {
        return m_slots.childrenOf(parentSlotId);
    }
    QList<registry::BoundUiAction> actionsIn(const QString &area) const override
    {
        return m_slots.actionsIn(area);
    }
    QList<registry::UiSlot> allSlots() const override { return m_slots.allSlots(); }

private:
    registry::UiSlotHub &m_slots;
};

// ---- IViewsService（裁定 52442006 Step 5）：壳读视图声明/打开视图的唯一通道。
class ViewsServiceAdapter final : public wb::IViewsService
{
public:
    ViewsServiceAdapter(registry::ViewRegistry &views) : m_views(views) {}

    QList<registry::ContributedViewContainer> containersIn(const QString &area) const override
    {
        QList<registry::ContributedViewContainer> out;
        for (const registry::ContributedViewContainer &c : m_views.allContainers())
        {
            if (c.area != area)
                continue;
            out.append(c);
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const registry::ContributedViewContainer &a,
                            const registry::ContributedViewContainer &b) {
                             return a.order != b.order ? a.order < b.order : a.id < b.id;
                         });
        return out;
    }
    QList<registry::ContributedView> viewsInContainer(const QString &containerId) const override
    {
        return m_views.viewsIn(containerId);
    }
    registry::ContributedView viewForId(const QString &viewId) const override
    {
        for (const registry::ContributedView &v : m_views.allViews())
            if (v.id == viewId)
                return v;
        return registry::ContributedView{};
    }
    QWidget *openView(const QString &viewId, ctx::AppContext &appCtx,
                      intent::IIntentExecutor &executor, QWidget *parent) override
    {
        return m_views.create(viewId, appCtx, executor, parent);
    }

private:
    registry::ViewRegistry &m_views;
};

} // namespace nextpilot::plotscope::assembly
