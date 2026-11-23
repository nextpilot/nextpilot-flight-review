#include "AnalysisModule.h"

#include "AppContext.h"
#include "BuiltinModuleHub.h"
#include "DataSource.h"
#include "Dataset.h"
#include "Contribution.h"
#include "IModule.h"
#include "Intent.h"
#include "RuntimeHost.h"
#include "Result.h"
#include "ErrorCodes.h"
#include "SettingsStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

// 动态组条目挂在独立 provider 下：重置时只清这一组，不影响其它模块动作。
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

} // namespace

// analysis.run / analysis.favorite 的处理器：分析模块只提供调度机制。
//   - application="default"：把活动数据源的全部数值通道铺成默认曲线（真实可用）；
//   - 其它 application：由领域扩展贡献，未注册时明确提示，绝不假装可用。
//   - analysis.favorite：切换收藏（Workspace settings 持久化），仅收藏已知应用。
// 最近使用在 run 成功后自动记录（MRU，最多 5 条）；最近/收藏两组条目
// 经 IUiContext 动态 provider 重置重建，外壳 rebuildRibbon 后 gallery 即时更新。
class AnalysisModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(AppContext &app, QObject *parent)
        : QObject(parent), m_app(app) {}

    void attachUi(IUiContext *ui) { m_ui = ui; }

    // onAttach 登记"全部应用"目录（也是收藏合法性的校验来源）。
    void setCatalog(const QStringList &catalog) { m_catalog = catalog; }

    // onAttach 阶段恢复持久化的"最近使用/收藏"两组（跨会话）。
    void rebuildDynamicGroupsForAttach() { rebuildDynamicGroups(); }

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
    // 应用目录（onAttach 时登记）：全部应用组展示的清单，也是收藏合法性校验的来源。
    QStringList m_catalog;
    IUiContext *m_ui = nullptr;

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
        m_app.settings().setJson(SettingsScope::Workspace, kFavoritesKey, stringListToJson(favs));
        rebuildDynamicGroups();
        m_app.settings().flush();

        IntentResult r;
        r.payload = QVariantMap{
            {QStringLiteral("application"), app},
            {QStringLiteral("favorite"), !wasFavorite},
        };
        return r;
    }

    QStringList recents() const
    {
        return jsonToStringList(m_app.settings().json(SettingsScope::Workspace, kRecentsKey),
                                QStringLiteral("apps"));
    }

    QStringList favorites() const
    {
        return jsonToStringList(m_app.settings().json(SettingsScope::Workspace, kFavoritesKey),
                                QStringLiteral("apps"));
    }

    // MRU 记录：先删旧位再插到头部，截断到上限。
    void pushRecent(const QString &app)
    {
        QStringList rec = recents();
        rec.removeAll(app);
        rec.prepend(app);
        while (rec.size() > kMaxRecents)
            rec.removeLast();
        m_app.settings().setJson(SettingsScope::Workspace, kRecentsKey, stringListToJson(rec));
        m_app.settings().flush();
        rebuildDynamicGroups();
    }

    // 重置"最近使用/收藏"两组动态条目（无 UI 时只更新 settings，headless 降级）。
    void rebuildDynamicGroups()
    {
        if (!m_ui)
            return;
        m_ui->resetDynamicActions(kDynamicProvider);
        const QString area = QStringLiteral("ribbon.group.analysis.apps");
        int order = 100;
        for (const QString &app : recents())
        {
            UiAction a = makeEntry(app, QStringLiteral("最近使用"), order++);
            m_ui->addDynamicAction(a, kDynamicProvider);
        }
        order = 200;
        for (const QString &app : favorites())
        {
            UiAction a = makeEntry(app, QStringLiteral("收藏"), order++);
            m_ui->addDynamicAction(a, kDynamicProvider);
        }
    }

    UiAction makeEntry(const QString &app, const QString &group, int order) const
    {
        UiAction a;
        a.id = QStringLiteral("analysis.app.%1.%2")
                   .arg(group == QStringLiteral("最近使用") ? QStringLiteral("recent")
                                                           : QStringLiteral("fav"),
                        app);
        a.label = displayLabel(app);
        a.area = QStringLiteral("ribbon.group.analysis.apps");
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
        const int sid = m_app.activeSourceId();
        const DataSourceInfo *src = m_app.sources().get(sid);
        if (!src)
            return fail(err::NoSource, QStringLiteral("没有活动数据源，无法生成默认曲线"));

        int channels = 0;
        for (int dsId : src->datasetIds)
        {
            const Dataset *ds = m_app.data().get(dsId);
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
                    m_app.setActiveChannel(dsId, tbl->name, col.name);
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

    AppContext &m_app;
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

IModule *AnalysisModule::create()
{
    return new AnalysisModule();
}

ModuleInfo AnalysisModule::info() const
{
    return moduleInfo();
}

void AnalysisModule::initialize(RuntimeHost &host)
{
    m_handler = new Handler(host.app(), this);
    host.registerIntentHandler(m_handler);
}

void AnalysisModule::onAttach(IUiContext &ui)
{
    m_handler->attachUi(&ui);

    ActivitySpec analysisActivity;
    analysisActivity.id = QStringLiteral("analysis");
    analysisActivity.label = QStringLiteral("分析");
    analysisActivity.icon = QStringLiteral("plot_refresh");
    analysisActivity.order = 2;
    analysisActivity.ribbonPage = QStringLiteral("ribbon.page.analysis");
    ui.declareActivity(analysisActivity);

    // 分析页"应用库"gallery："全部/最近/收藏"三组。
    const QString area = QStringLiteral("ribbon.group.analysis.apps");
    // gallery 分组出现顺序：全部应用(0..) → 最近使用(100+) → 收藏(200+)。
    auto entry = [&ui, &area](const QString &id, const QString &label,
                              const QString &app, const QString &group, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.area = area;
        a.intentOp = QStringLiteral("analysis.run");
        a.data = app;
        a.icon = QStringLiteral("chart");
        a.visual = QStringLiteral("timeseries");
        a.galleryGroup = group;
        a.order = order;
        ui.addAction(a);
    };

    // "全部应用"组：默认曲线（真实可用）+ 两个领域应用占位（点了给出诚实提示）。
    // 目录同时是收藏合法性校验的来源——Handler 只收藏清单里的应用。
    const QStringList catalog = {
        QStringLiteral("default"),
        QStringLiteral("autopilot.review.report"),
        QStringLiteral("autopilot.review.app"),
    };
    m_handler->setCatalog(catalog);
    entry(QStringLiteral("analysis.app.default"), QStringLiteral("默认曲线"),
          QStringLiteral("default"), QStringLiteral("全部应用"), 0);
    entry(QStringLiteral("analysis.app.report"), QStringLiteral("分析报告"),
          QStringLiteral("autopilot.review.report"), QStringLiteral("全部应用"), 1);
    entry(QStringLiteral("analysis.app.review"), QStringLiteral("飞控复盘"),
          QStringLiteral("autopilot.review.app"), QStringLiteral("全部应用"), 2);

    // "最近使用/收藏"两组由 Handler 从 Workspace settings 恢复（跨会话持久）。
    m_handler->rebuildDynamicGroupsForAttach();
}

#include "AnalysisModule.moc"

PS_REGISTER_MODULE(AnalysisModule)
