#include "AnalysisModule.h"

#include "AppContext.h"
#include "BuiltinModuleRegistry.h"
#include "DataSource.h"
#include "Dataset.h"
#include "IUiContribution.h"
#include "IModule.h"
#include "Intent.h"
#include "ModuleHost.h"
#include "Result.h"

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::host;

// analysis.run 的处理器：分析器官只提供调度机制。
//   - application="default"：把活动数据源的全部数值通道铺成默认曲线（真实可用）；
//   - 其它 application：由领域扩展贡献，未注册时明确提示，绝不假装可用。
class AnalysisModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(AppContext &app, QObject *parent)
        : QObject(parent), m_app(app) {}

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("analysis.run");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        const QString app = intent.params.value(QStringLiteral("application")).toString();
        if (app.isEmpty() || app == QStringLiteral("default"))
            return runDefault();

        // 领域应用（飞控复盘等）由 extensions 贡献；此处没有对应实现就诚实返回失败，
        // 而不是弹一个空壳面板。
        return fail(QStringLiteral("enoimpl"),
                    QStringLiteral("分析应用「%1」由领域扩展提供，当前未注册").arg(app));
    }

private:
    Result<IntentResult> runDefault()
    {
        const int sid = m_app.activeSourceId();
        const DataSourceInfo *src = m_app.sources().get(sid);
        if (!src)
            return fail(QStringLiteral("enosource"), QStringLiteral("没有活动数据源，无法生成默认曲线"));

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
            return fail(QStringLiteral("echan"), QStringLiteral("活动数据源没有可绘制的数值通道"));

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
        ModuleKind::Builtin,
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

void AnalysisModule::initialize(ModuleContext &ctx)
{
    m_handler = new Handler(ctx.app(), this);
    ctx.registerIntentHandler(m_handler);
}

void AnalysisModule::onAttach(UiContext &ui)
{
    // 分析页"应用库"gallery：抄老 AnalysisRibbon 的"全部/最近/收藏"分组体验。
    // 当前内置一个可用的 default 应用；领域应用由扩展注册后也能往这个 area 加项。
    const QString area = QStringLiteral("ribbon.group.analysis.apps");
    // gallery 分组出现顺序抄老 AnalysisRibbon：全部应用(0..) → 最近使用(100+) → 收藏(200+)。
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
    entry(QStringLiteral("analysis.app.default"), QStringLiteral("默认曲线"),
          QStringLiteral("default"), QStringLiteral("全部应用"), 0);
    entry(QStringLiteral("analysis.app.report"), QStringLiteral("分析报告"),
          QStringLiteral("autopilot.review.report"), QStringLiteral("全部应用"), 1);
    entry(QStringLiteral("analysis.app.review"), QStringLiteral("飞控复盘"),
          QStringLiteral("autopilot.review.app"), QStringLiteral("全部应用"), 2);
    // "最近使用"组：默认曲线（抄老代码把 default 作为默认最近项）。
    entry(QStringLiteral("analysis.app.recent.default"), QStringLiteral("默认曲线"),
          QStringLiteral("default"), QStringLiteral("最近使用"), 100);
}

#include "AnalysisModule.moc"

NG_REGISTER_MODULE(AnalysisModule)
