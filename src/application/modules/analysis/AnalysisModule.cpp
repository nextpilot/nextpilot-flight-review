#include "AnalysisModule.h"

#include "AppContext.h"
#include "BuiltinModuleHub.h"
#include "DataSource.h"
#include "Dataset.h"
#include "Contribution.h"
#include "IWorkbenchContribution.h"
#include "InstantiationService.h"
#include "IWorkspace.h"
#include "IWindow.h"
#include "ICommands.h"
#include "CommandAdapter.h"
#include "Intent.h"
#include "RuntimeHost.h"
#include "Result.h"
#include "ErrorCodes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using nextpilot::plotscope::instantiation::InstantiationService;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

namespace
{

// 动态组条目挂在独立 provider 下：提供器按 id 归属，卸载/诊断只清这一组。
const QString kDynamicProvider = QStringLiteral("module:analysis-dynamic");
// settings（Workspace 作用域）持久化键：跨会话恢复"最近使用/收藏"。
const QString kRecentsKey = QStringLiteral("analysis/recents");
const QString kFavoritesKey = QStringLiteral("analysis/favorites");
// 最近使用最多保留 5 条。
constexpr int kMaxRecents = 5;

QStringList jsonToStringList(const QJsonObject &obj, const QString &key)
{
    QStringList out;
    const QJsonArray arr = obj.value(key).toArray();
    for (const QJsonValue &v : arr)
    {
        const QString s = v.toString();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

QJsonObject stringListToJson(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list)
        arr.append(s);
    return QJsonObject{{QStringLiteral("apps"), arr}};
}

// 配置扁平映射：SettingsStore::setJson(Workspace, key, obj) 的扁平 key 为
// "ws/"+key（scopePrefix(Workspace)="ws"），值存 JSON 紧凑文本；读时反向解析。
// 这样 handler 只经 IWorkspace 的扁平 get/setConfiguration，不出现 SettingsStore 类型。
QString wsConfigKey(const QString &key) { return QStringLiteral("ws/") + key; }

QJsonObject readWsJson(const nextpilot::plotscope::workbench::IWorkspace &ws, const QString &key)
{
    const QVariant v = ws.getConfiguration(wsConfigKey(key));
    if (v.typeId() == QMetaType::QString)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isObject())
            return doc.object();
    }
    if (v.canConvert<QJsonObject>())
        return v.toJsonObject();
    return {};
}

void writeWsJson(nextpilot::plotscope::workbench::IWorkspace &ws, const QString &key, const QJsonObject &obj)
{
    const QString text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    ws.setConfiguration(wsConfigKey(key), QVariant(text));
}

} // namespace

// analysis.run / analysis.favorite 的处理器：分析模块只提供调度机制。
//   - application="default"：把活动数据源的全部数值通道铺成默认曲线（真实可用）；
//   - 其它 application：由领域扩展贡献，未注册时明确提示，绝不假装可用。
//   - analysis.favorite：切换收藏（Workspace settings 持久化），仅收藏已知应用。
// 最近使用在 run 成功后自动记录（MRU，最多 5 条）；最近/收藏两组条目由
// **动作提供器**在 ribbon 装配时实时从 settings 取数（契约 §3.3 / D10），
// 内容变化后调 refreshActions() 触发外壳重建，不再走"清空再全量重注册"的动态通道。
class AnalysisModule::Handler final : public QObject, public IIntentHandler,
                                      public IActionProvider
{
    Q_OBJECT
public:
    explicit Handler(nextpilot::plotscope::workbench::IWorkspace &ws, nextpilot::plotscope::workbench::IWindow &window,
                     QObject *parent)
        : QObject(parent), m_ws(ws), m_window(window)
    {
    }

    // onAttach 登记"全部应用"目录（也是收藏合法性的校验来源）。
    void setCatalog(const QStringList &catalog) { m_catalog = catalog; }

    // IActionProvider（裁定 52441074 §2）：动态 gallery 换成对象接口，lambda 不进壳。
    // 返回纯数据 UiAction，headless 可查；变化经 onDidChangeActions 通知外壳重建该 slot。
    QList<UiAction> actionsFor(const QString &slotId) const override
    {
        Q_UNUSED(slotId);
        QList<UiAction> out;
        int order = 100;
        for (const QString &app : recents())
            out << makeEntry(app, QStringLiteral("最近使用"), order++);
        order = 200;
        for (const QString &app : favorites())
            out << makeEntry(app, QStringLiteral("收藏"), order++);
        return out;
    }

    QMetaObject::Connection
    onDidChangeActions(std::function<void(const QString &slotId)> listener) override
    {
        if (!listener)
            return {};
        return QObject::connect(this, &Handler::actionsChanged, this,
                                [listener = std::move(listener)](const QString &slotId) {
                                    listener(slotId);
                                });
    }

    // 通知变化：收藏/最近条目变更后发信号，外壳只重建对应 slot（不再全局 refreshActions）。
    void notifyActionsChanged()
    {
        Q_EMIT actionsChanged(QStringLiteral("ribbon.group.analysis.apps"));
    }

signals:
    void actionsChanged(const QString &slotId);

public:

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("analysis.run")
               || intent.op == QStringLiteral("analysis.favorite");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        if (intent.op == QStringLiteral("analysis.favorite"))
            return toggleFavorite(intent);
        return run(intent);
    }

private:
    // 应用目录（initialize 时登记）：全部应用组展示的清单，也是收藏合法性校验的来源。
    QStringList m_catalog;

    Result<IntentResult> run(const Intent &intent)
    {
        const QString app = intent.params.value(QStringLiteral("application")).toString();
        if (app.isEmpty() || app == QStringLiteral("default"))
        {
            const auto r = runDefault();
            if (r)
                pushRecent(QStringLiteral("default"));
            return r;
        }

        // 领域应用（飞控复盘等）由 extensions 贡献；此处没有对应实现就诚实返回失败，
        // 而不是弹一个空壳面板。
        return fail(err::NoImpl,
                    QStringLiteral("分析应用「%1」由领域扩展提供，当前未注册").arg(app));
    }

    // 收藏切换：已在收藏则移除，不在则加入（仅允许收藏目录里的应用）。
    Result<IntentResult> toggleFavorite(const Intent &intent)
    {
        const QString app = intent.params.value(QStringLiteral("application")).toString();
        if (app.isEmpty())
            return fail(err::Param, QStringLiteral("缺少 application 参数"));
        if (!m_catalog.contains(app))
            return fail(err::NoImpl,
                        QStringLiteral("分析应用「%1」未注册，不能收藏").arg(app));

        QStringList favs = favorites();
        const bool wasFavorite = favs.contains(app);
        if (wasFavorite)
            favs.removeAll(app);
        else
            favs << app;
        writeWsJson(m_ws, kFavoritesKey, stringListToJson(favs));
        notifyActionsChanged(); // IActionProvider 对象接口：通知外壳重建该 slot
        m_ws.flushConfiguration();

        IntentResult r;
        r.payload = QVariantMap{
            {QStringLiteral("application"), app},
            {QStringLiteral("favorite"), !wasFavorite},
        };
        return r;
    }

    QStringList recents() const { return jsonToStringList(readWsJson(m_ws, kRecentsKey), QStringLiteral("apps")); }

    QStringList favorites() const { return jsonToStringList(readWsJson(m_ws, kFavoritesKey), QStringLiteral("apps")); }

    // MRU 记录：先删旧位再插到头部，截断到上限。
    void pushRecent(const QString &app)
    {
        QStringList rec = recents();
        rec.removeAll(app);
        rec.prepend(app);
        while (rec.size() > kMaxRecents)
            rec.removeLast();
        writeWsJson(m_ws, kRecentsKey, stringListToJson(rec));
        m_ws.flushConfiguration();
        notifyActionsChanged(); // IActionProvider 对象接口：MRU 变化通知
    }

    UiAction makeEntry(const QString &app, const QString &group, int order) const
    {
        UiAction a;
        a.id = QStringLiteral("analysis.app.%1.%2")
                   .arg(group == QStringLiteral("最近使用") ? QStringLiteral("recent")
                                                           : QStringLiteral("fav"),
                        app);
        a.label = displayLabel(app);
        a.slotId = QStringLiteral("ribbon.group.analysis.apps");
        a.intentOp = QStringLiteral("analysis.run");
        a.data = app;
        a.icon = QStringLiteral("chart");
        a.visual = QStringLiteral("timeseries");
        a.galleryGroup = group;
        a.order = order;
        return a;
    }

    QString displayLabel(const QString &app) const
    {
        if (app == QStringLiteral("default"))
            return QStringLiteral("默认曲线");
        if (app == QStringLiteral("autopilot.review.report"))
            return QStringLiteral("分析报告");
        if (app == QStringLiteral("autopilot.review.app"))
            return QStringLiteral("飞控复盘");
        return app;
    }

    Result<IntentResult> runDefault()
    {
        const int sid = m_window.activeSource();
        const DataSourceInfo *src = m_ws.sources().get(sid);
        if (!src)
            return fail(err::NoSource, QStringLiteral("没有活动数据源，无法生成默认曲线"));

        int channels = 0;
        for (int dsId : src->datasetIds)
        {
            const Dataset *ds = m_ws.dataset().get(dsId);
            if (!ds)
                continue;
            for (int t = 0; t < ds->tableCount(); ++t)
            {
                const Table *tbl = ds->table(t);
                if (!tbl)
                    continue;
                for (const Column &col : tbl->columns)
                {
                    if (col.type == ValueType::Text)
                        continue;
                    m_window.setActiveChannel(dsId, tbl->name, col.name);
                    ++channels;
                }
            }
        }
        if (channels == 0)
            return fail(err::Chan, QStringLiteral("活动数据源没有可绘制的数值通道"));

        IntentResult r;
        r.payload = QVariantMap{
            {QStringLiteral("application"), QStringLiteral("default")},
            {QStringLiteral("channels"), channels},
        };
        return r;
    }

    nextpilot::plotscope::workbench::IWorkspace &m_ws;
    nextpilot::plotscope::workbench::IWindow &m_window;
};

ModuleInfo AnalysisModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.analysis"),
        QStringLiteral("分析模块"),
        QStringLiteral("1.0"),
        {QStringLiteral("analysis.runner")},
        {}
    };
}

IWorkbenchContribution *AnalysisModule::create() { return new AnalysisModule(); }

ModuleInfo AnalysisModule::info() const
{
    return moduleInfo();
}

void AnalysisModule::initialize(InstantiationService &services)
{
    auto *ws = services.getService<nextpilot::plotscope::workbench::IWorkspace>(QStringLiteral("vscode.workspace"));
    auto *window = services.getService<nextpilot::plotscope::workbench::IWindow>(QStringLiteral("vscode.window"));
    auto *cmds = services.getService<nextpilot::plotscope::workbench::ICommands>(QStringLiteral("vscode.commands"));
    if (!ws || !window || !cmds)
        return;
    m_handler = new Handler(*ws, *window, this);
    // 应用目录（收藏合法性校验来源）：从 initialize 起即就绪，不依赖 UI（A8b-M2）。
    m_handler->setCatalog({QStringLiteral("default"),
                           QStringLiteral("autopilot.review.report"),
                           QStringLiteral("autopilot.review.app")});
    auto reg = [cmds](const QString &id, nextpilot::plotscope::intent::IIntentHandler *h) {
        cmds->registerCommand(id, nextpilot::plotscope::workbench::asCommandHandler(h, id));
    };
    reg(QStringLiteral("analysis.run"), m_handler);
    reg(QStringLiteral("analysis.favorite"), m_handler);
}

ModuleContributions AnalysisModule::contributes() const
{
    ModuleContributions c;
    c.owner = QStringLiteral("analysis");
    c.ribbonPages.append(RibbonPageDecl{QStringLiteral("ribbon.page.analysis"),
                                        QStringLiteral("分析"), 6});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.analysis.apps"),
                                          QStringLiteral("应用库"),
                                          QStringLiteral("ribbon.page.analysis"), 0});
    ActivitySpec analysisActivity;
    analysisActivity.id = QStringLiteral("analysis");
    analysisActivity.label = QStringLiteral("分析");
    analysisActivity.icon = QStringLiteral("plot_refresh");
    analysisActivity.order = 2;
    analysisActivity.ribbonPage = QStringLiteral("ribbon.page.analysis");
    c.activities.append(analysisActivity);

    // 分析页"应用库"gallery："全部/最近/收藏"三组。
    const QString slotId = QStringLiteral("ribbon.group.analysis.apps");
    auto entry = [&c, &slotId](const QString &id, const QString &label,
                               const QString &app, const QString &group, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = slotId;
        a.intentOp = QStringLiteral("analysis.run");
        a.data = app;
        a.icon = QStringLiteral("chart");
        a.visual = QStringLiteral("timeseries");
        a.galleryGroup = group;
        a.order = order;
        c.actions.append(a);
    };
    // "全部应用"组（真实默认 + 两个诚实占位）。
    entry(QStringLiteral("analysis.app.default"), QStringLiteral("默认曲线"),
          QStringLiteral("default"), QStringLiteral("全部应用"), 0);
    entry(QStringLiteral("analysis.app.report"), QStringLiteral("分析报告"),
          QStringLiteral("autopilot.review.report"), QStringLiteral("全部应用"), 1);
    entry(QStringLiteral("analysis.app.review"), QStringLiteral("飞控复盘"),
          QStringLiteral("autopilot.review.app"), QStringLiteral("全部应用"), 2);

    // 动态"最近/收藏"：裁定 §2 换成 provider 对象（handler 即 IActionProvider），
    // lambda 不进壳；变化经 onDidChangeActions 通知，外壳只重建该 slot。
    if (m_handler)
        c.actionProviders.append(ActionProviderDecl{QStringLiteral("analysis"),
                                                    slotId, m_handler});
    return c;
}

void AnalysisModule::onAttach(IUiContext &ui)
{
    Q_UNUSED(ui);
    // A8b-M2：UI 声明（含动态 provider）已迁至 contributes()，宿主统一灌回。
}

#include "AnalysisModule.moc"

PS_REGISTER_MODULE(AnalysisModule)
