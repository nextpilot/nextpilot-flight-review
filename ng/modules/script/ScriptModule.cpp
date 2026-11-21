#include "ScriptModule.h"

#include "BuiltinModuleRegistry.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "LuaEngine.h"
#include "ScriptFacade.h"
#include "TransformEvaluator.h"
#include "MarkerEngine.h"
#include "AppContext.h"
#include "RuntimeHost.h"

#include <QByteArray>
#include <QFile>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

// script.run 处理器：接受 {source=...} 或 {path=...}，组装门面并执行脚本。
// 门面绑定宿主执行器 + 可选录制器，因此脚本调用与 GUI 点击走同一条执行路径。
class ScriptModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(IIntentExecutor &executor, JobRecorder *recorder,
                     ScriptFacade::ViewActionDispatcher viewDispatcher, QObject *parent)
        : QObject(parent), m_executor(executor), m_recorder(recorder),
          m_viewDispatcher(std::move(viewDispatcher)) {}

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("script.run");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        QByteArray source;
        if (intent.params.contains(QStringLiteral("source")))
        {
            source = intent.params.value(QStringLiteral("source")).toByteArray();
        }
        else if (intent.params.contains(QStringLiteral("path")))
        {
            const QString path = intent.params.value(QStringLiteral("path")).toString();
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                return fail(err::Io, QStringLiteral("无法读取脚本: %1").arg(path));
            source = f.readAll();
        }
        else
        {
            return fail(err::Args, QStringLiteral("script.run 需要 source 或 path"));
        }

        // 门面是栈对象，仅本次执行有效；脚本拿不到它的指针。
        // 注入 view dispatcher 后，脚本 app.call 可触达 GUI 视图能力。
        ScriptFacade facade(m_executor, m_recorder, m_viewDispatcher);
        nextpilot::plotscope::script::LuaEngine lua(facade);
        Result<void> r = lua.runString(source, QStringLiteral("=script.run"));
        if (!r)
            return fail(r.error().code, r.error().message);

        IntentResult out;
        out.payload = QStringLiteral("ok");
        return out;
    }

private:
    IIntentExecutor &m_executor;
    JobRecorder *m_recorder = nullptr;
    ScriptFacade::ViewActionDispatcher m_viewDispatcher;
};

ModuleInfo ScriptModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.script"),
        QStringLiteral("脚本模块"),
        QStringLiteral("1.0"),
        ModuleKind::Builtin,
        {QStringLiteral("script.runner")},
        {} // 不强依赖具体模块：脚本能力通过门面按需发意图
    };
}

IModule *ScriptModule::create()
{
    return new ScriptModule();
}

ModuleInfo ScriptModule::info() const
{
    return moduleInfo();
}

void ScriptModule::initialize(ModuleContext &ctx)
{
    // view action 派发器：脚本 app.call 在意图通道 eunsupported 时回退到这里，
    // 从而能触发 plot.*/page.*/playback.* 等 GUI 视图能力（headless 下也可注册但通常无视图）。
    RuntimeHost *host = &ctx.host();
    ScriptFacade::ViewActionDispatcher viewDispatcher =
        [host](const QString &id, const QVariantMap &params, Result<QVariant> &out) {
            return host->dispatchViewAction(id, params, out);
        };
    m_handler = new Handler(ctx.executor(), ctx.recorder(), std::move(viewDispatcher), this);
    ctx.registerIntentHandler(m_handler);

    // 统一变换求值：注册进 AppContext，供 host(数据源衍生)/gui(曲线)/protocol(checksum)
    // 调用，而不必反向依赖 script 模块。builtin/expression 内部走 Rust，lua 走 C++。
    // 捕获 AppContext 指针（其生命周期 = 进程运行期，长于回调）。
    nextpilot::plotscope::context::AppContext *appCtx = &ctx.app();
    ctx.host().registerTransformEvaluator(
        [appCtx](const QJsonObject &request, uint32_t cap,
                 const QVector<double> &t, const QVector<double> &v) {
            return nextpilot::plotscope::script::TransformEvaluator::evalSeries(
                appCtx->transforms(), request, cap, t, v);
        });

    // 有状态过滤器能力：内置目录注入 InteractionRegistry，供 host(data.filter) 与 GUI 编辑器调用。
    // 目录归模块持有（生命周期）；回调只吃/吐 JSON，调用方不依赖 script 模块类型。
    m_filterCatalogue = nextpilot::plotscope::script::makeBundledFilterCatalogue();
    auto *cat = m_filterCatalogue.get();
    nextpilot::plotscope::uischema::FilterOps filterOps;
    filterOps.list = [cat]() -> QJsonArray {
        QJsonArray arr;
        for (const auto &e : cat->entries())
            arr.append(nextpilot::plotscope::script::FilterCatalogue::descriptorToJson(e.cls));
        return arr;
    };
    filterOps.describe = [cat](const QString &id) -> Result<QJsonObject> {
        const auto *e = cat->find(id);
        if (!e)
            return fail(err::NoEnt, QStringLiteral("未知过滤器: %1").arg(id));
        return Result<QJsonObject>(
            nextpilot::plotscope::script::FilterCatalogue::descriptorToJson(e->cls));
    };
    filterOps.applySeries = [cat](const QString &id, const QJsonObject &params,
                                  const QVector<double> &t, const QVector<double> &v) {
        return cat->applySeries(id, params, t, v);
    };
    ctx.host().registerFilterOps(std::move(filterOps));

    // Marker 规则能力：run(code, seriesJson) -> markers JSON。用一次性 MarkerEngine 执行，
    // seriesJson 里的序列在本 lambda 栈上构造，provider 的 get 返回它们的地址（调用期有效）。
    nextpilot::plotscope::uischema::MarkerOps markerOps;
    markerOps.run = [](const QByteArray &code, const QJsonArray &seriesJson) -> Result<QJsonArray> {
        using namespace nextpilot::plotscope::script;
        QVector<MarkerSeries> owned;
        owned.reserve(seriesJson.size());
        for (const QJsonValue &jv : seriesJson)
        {
            const QJsonObject o = jv.toObject();
            MarkerSeries s;
            s.name = o.value(QStringLiteral("name")).toString();
            for (const QJsonValue &t : o.value(QStringLiteral("time")).toArray())
                s.time.append(t.toDouble());
            for (const QJsonValue &v : o.value(QStringLiteral("value")).toArray())
                s.value.append(v.toDouble());
            owned.append(std::move(s));
        }
        MarkerSeriesProvider provider;
        for (const MarkerSeries &s : owned)
            provider.names << s.name;
        provider.get = [&owned](const QString &name) -> const MarkerSeries * {
            for (const MarkerSeries &s : owned)
                if (s.name == name)
                    return &s;
            return nullptr;
        };
        MarkerEngine eng;
        auto r = eng.run(code, provider);
        if (!r)
            return fail(r.error().code, r.error().message);
        return Result<QJsonArray>(markersToJson(r.value()));
    };
    ctx.host().registerMarkerOps(std::move(markerOps));

    // Lua 脚本库：目录可用 NG_SCRIPTS_DIR 覆盖（部署/测试），默认 scripts/ops。
    // 注册表注入：脚本算子登记进 AppContext 持有的 TransformRegistry 实例。
    const QByteArray dirOverride = qgetenv("NG_SCRIPTS_DIR");
    m_scriptLib = std::make_unique<nextpilot::plotscope::script::LuaScriptLibrary>(ctx.app().transforms());
    m_scriptLib->setOpsDir(dirOverride.isEmpty() ? QStringLiteral("scripts/ops")
                                                  : QString::fromLocal8Bit(dirOverride.constData()));
    m_scriptLib->loadAll();

    TransformScriptOps ops;
    ops.resolve = [this](const QString &scriptId, const QString &snapshot) {
        return m_scriptLib->resolveSource(scriptId, snapshot);
    };
    ops.save = [this](const QString &name, const QByteArray &source) {
        return m_scriptLib->save(name, source);
    };
    ops.describe = [this](const QByteArray &source, const QString &name) {
        return m_scriptLib->describe(source, name);
    };
    ops.list = [this]() { return m_scriptLib->list(); };
    ops.remove = [this](const QString &name) { return m_scriptLib->remove(name); };
    ctx.host().registerTransformScriptOps(ops);

    // op 目录：工作台速查面板的数据源。存 provider 实时拉取，与实际可执行意图一致
    // （handler 是各模块 initialize 陆续注册的，快照会漏后注册者）。
    RuntimeHost *hostPtr = &ctx.host();
    ctx.app().interactions().registerOpCatalogue([hostPtr]() {
        return hostPtr->registeredOps();
    });

    // 工作台执行通道：GUI 面板（编辑器/控制台）经 viewAction 调用，引擎归模块——
    // GUI 层不直接依赖 script 模块内部类（层契约：ng_module_script_gui 禁止横向
    // 依赖 ng_module_script），输出回显经 script.console.append 回面板。
    registerWorkbenchActions(ctx, std::move(viewDispatcher));
}

void ScriptModule::registerWorkbenchActions(ModuleContext &ctx, ScriptFacade::ViewActionDispatcher dispatcher)
{
    IIntentExecutor &executor = ctx.executor();
    JobRecorder *recorder = ctx.recorder();

    // 输出回显：经 InteractionRegistry 发 script.console.append（GUI 控制台面板注册的处理
    // 器接收）。headless 下没有面板，viewAction 不存在——静默忽略，不影响执行。
    nextpilot::plotscope::context::InteractionRegistry *inter = &ctx.app().interactions();
    auto echo = [inter](const QString &text, bool error) {
        QVariantMap p;
        p.insert(QStringLiteral("text"), text);
        p.insert(QStringLiteral("error"), error);
        Result<QVariant> out{QVariant()};
        (void)inter->dispatchViewAction(QStringLiteral("script.console.append"), p, out);
    };

    // script.workbench.run：运行编辑器当前文档。引擎每次新建（与 script.run 同路径）。
    ctx.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.run"),
        [&executor, recorder, dispatcher, echo](const QVariantMap &params) -> Result<QVariant> {
            const QByteArray source = params.value(QStringLiteral("source")).toByteArray();
            const QString name = params.value(QStringLiteral("name")).toString();
            ScriptFacade facade(executor, recorder, dispatcher);
            nextpilot::plotscope::script::LuaEngine engine(facade);
            engine.setOutputSink([&echo](const QString &m) { echo(m, false); });
            const Result<void> r = engine.runString(source, QStringLiteral("@%1").arg(name));
            if (!r)
                return fail(r.error().code, r.error().message);
            return Result<QVariant>(QVariant(true));
        });

    // script.workbench.repl：控制台求值。持久 VM 归模块持有，进程期共享全局状态。
    ctx.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.repl"),
        [this, &executor, dispatcher, echo](const QVariantMap &params) -> Result<QVariant> {
            const QString code = params.value(QStringLiteral("code")).toString();
            if (!m_replEngine)
            {
                m_replFacade = std::make_unique<ScriptFacade>(executor, nullptr, dispatcher);
                m_replEngine = std::make_unique<nextpilot::plotscope::script::LuaEngine>(*m_replFacade);
                m_replEngine->setOutputSink([echo](const QString &m) { echo(m, false); });
            }
            const Result<QVariant> r = m_replEngine->evaluate(code);
            if (!r)
                return fail(r.error().code, r.error().message);
            QVariant out;
            if (!r.value().isNull() && r.value().isValid())
                out = r.value();
            return Result<QVariant>(out);
        });

    // script.workbench.open / .new：确保编辑器面板存在（按需创建进中央区页签），
    // 再转发给编辑器面板打开/新建文档。headless 下 m_ui 为空，明确报错。
    auto ensureEditor = [this](Result<QVariant> &out) -> bool {
        if (!m_ui)
        {
            out = fail(err::Unsupported, QStringLiteral("脚本编辑器需要 GUI 环境"));
            return false;
        }
        return m_ui->requestPanel(QStringLiteral("canvas.center"),
                                  QStringLiteral("script-editor")) != nullptr;
    };
    nextpilot::plotscope::context::InteractionRegistry *reg = &ctx.app().interactions();
    ctx.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.open"),
        [this, ensureEditor, reg](const QVariantMap &params) -> Result<QVariant> {
            Result<QVariant> out{QVariant()};
            if (!ensureEditor(out))
                return out;
            reg->dispatchViewAction(QStringLiteral("script.open"), params, out);
            return out;
        });
    ctx.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.new"),
        [this, ensureEditor, reg](const QVariantMap &params) -> Result<QVariant> {
            Result<QVariant> out{QVariant()};
            if (!ensureEditor(out))
                return out;
            reg->dispatchViewAction(QStringLiteral("script.new"), params, out);
            return out;
        });
}

ScriptModule::~ScriptModule() = default;

void ScriptModule::onAttach(UiContext &ui)
{
    // GUI 环境下才走到这里（headless 时宿主不调用 onAttach）。
    //
    // 三个侧边停靠面板随 attach 挂上（与既有面板同侧自动并成页签，零外壳改动）；
    // **编辑器不在此创建**——模块激活顺序不定，编辑器抢先会占住中央 dock、
    // 把画布挤成页签。编辑器按需创建（首次打开/新建脚本时）：
    // script.workbench.open / .new 先 ensureEditor 再转发给编辑器面板。
    // 面板实现由视图层自注册（ScriptPanels.cpp 的 NG_REGISTER_PANEL_T），
    // 模块只发字符串 id，不依赖 Qt Widgets，headless 构建保持纯净。
    m_ui = &ui;
    ui.requestPanel(QStringLiteral("dock.left"), QStringLiteral("script-files"));
    ui.requestPanel(QStringLiteral("dock.bottom"), QStringLiteral("script-console"));
    ui.requestPanel(QStringLiteral("dock.right"), QStringLiteral("script-debug"));
}

#include "ScriptModule.moc"

#include "ErrorCodes.h"
// 编译期自注册：新增脚本模块无需改 App / 宿主。
NG_REGISTER_MODULE(ScriptModule)
