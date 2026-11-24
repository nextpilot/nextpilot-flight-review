#include "ScriptModule.h"

#include "BuiltinModuleHub.h"
#include "BatchRunner.h"
#include "PythonHandler.h"
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

void ScriptModule::initialize(RuntimeHost &host)
{
    // view action 派发器：脚本 app.call 在意图通道 eunsupported 时回退到这里，
    // 从而能触发 plot.*/page.*/playback.* 等 GUI 视图能力（headless 下也可注册但通常无视图）。
    RuntimeHost *hostPtr = &host;
    ScriptFacade::ViewActionDispatcher viewDispatcher =
        [hostPtr](const QString &id, const QVariantMap &params, Result<QVariant> &out) {
            return hostPtr->dispatchViewAction(id, params, out);
        };
    m_handler = new Handler(host, host.recorder(), viewDispatcher, this);
    host.registerIntentHandler(m_handler);

    // ---- 批量处理：data.batch.run 意图 + 取消通道 ----
    // 每文件 load→script 走与单文件完全相同的执行器路径；进度经 view action
    // data.batch.progress 广播（GUI 汇总面板消费），取消经 data.batch.cancel 置标志。
    m_batch = new nextpilot::plotscope::script::BatchHandler(
        host, &host.app().interactions(), &host.app().data(), &host.app().sources(),
        viewDispatcher, this);
    host.registerIntentHandler(m_batch);

    // ---- Python 子进程桥：script.py.* 意图族 ----
    // 常驻桥进程按需拉起（首个 script.py.* 调用时启动）；找不到解释器时
    // 每条意图返回明确错误（优雅降级，不弹窗轰炸）。
    m_py = new nextpilot::plotscope::script::PythonHandler(
        host, &host.app().data(), this);
    host.registerIntentHandler(m_py);
    host.app().interactions().registerViewAction(
        QStringLiteral("data.batch.cancel"),
        [this](const QVariantMap &) -> Result<QVariant> {
            if (!m_batch || !m_batch->running())
                return fail(err::Args, QStringLiteral("没有进行中的批量运行"));
            m_batch->requestCancel();
            return Result<QVariant>(QVariant(true));
        });

    // script.workbench.batch：批量运行入口（Ribbon"批量运行"按钮 / 脚本同路径）。
    // 先确保汇总面板存在，再经视图层 data.batch.pick 拿 {dir, script}
    // （无参弹文件对话框；带参直接透传——脚本/测试可免对话框驱动），
    // 然后执行 data.batch.run 意图，结束把汇总 payload 发给面板恢复按钮态。
    nextpilot::plotscope::context::InteractionHub *batchReg = &host.app().interactions();
    IIntentExecutor *batchExecutor = &host;
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.batch"),
        [this, batchReg, batchExecutor](const QVariantMap &params) -> Result<QVariant> {
            if (!m_batch)
                return fail(err::Unsupported, QStringLiteral("批量处理不可用"));
            if (!m_ui)
                return fail(err::Unsupported, QStringLiteral("批量汇总需要 GUI 环境"));
            if (!m_ui->requestPanel(nextpilot::plotscope::registry::Region::Editor,
                                    QStringLiteral("batch-summary")))
                return fail(err::Unsupported, QStringLiteral("批量汇总面板不可用"));
            QVariantMap picked = params;
            if (!picked.contains(QStringLiteral("dir")))
            {
                Result<QVariant> po{QVariant()};
                if (!batchReg->dispatchViewAction(QStringLiteral("data.batch.pick"),
                                                  QVariantMap(), po)
                    || !po.value().isValid())
                    return fail(err::Args, QStringLiteral("未选择批量目录/脚本"));
                picked = po.value().toMap();
            }
            if (!picked.contains(QStringLiteral("dir"))
                || (!picked.contains(QStringLiteral("script"))
                    && !picked.contains(QStringLiteral("source"))))
                return fail(err::Args, QStringLiteral("批量运行需要 dir 与 script/source"));
            Intent bi;
            bi.op = QStringLiteral("data.batch.run");
            bi.params = picked;
            Result<IntentResult> r = batchExecutor->execute(bi);
            // 结束汇总（面板据 cancelled/done 恢复按钮态、显示成败计数）
            QVariantMap sm = r ? r.value().payload.toMap() : QVariantMap();
            sm.insert(QStringLiteral("done"), true);
            if (!r)
                sm.insert(QStringLiteral("error"), r.error().message);
            Result<QVariant> so{QVariant()};
            batchReg->dispatchViewAction(QStringLiteral("data.batch.summary"), sm, so);
            if (!r)
                return fail(r.error().code, r.error().message);
            return Result<QVariant>(r.value().payload);
        });

    // 统一变换求值：注册进 AppContext，供 host(数据源衍生)/gui(曲线)/protocol(checksum)
    // 调用，而不必反向依赖 script 模块。builtin/expression 内部走 Rust，lua 走 C++。
    // 捕获 AppContext 指针（其生命周期 = 进程运行期，长于回调）。
    nextpilot::plotscope::context::AppContext *appCtx = &host.app();
    host.registerTransformEvaluator(
        [appCtx](const QJsonObject &request, uint32_t cap,
                 const QVector<double> &t, const QVector<double> &v) {
            return nextpilot::plotscope::script::TransformEvaluator::evalSeries(
                appCtx->transforms(), request, cap, t, v);
        });

    // 有状态过滤器能力：内置目录注入 InteractionHub，供 host(data.filter) 与 GUI 编辑器调用。
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
    host.registerFilterOps(std::move(filterOps));

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
    host.registerMarkerOps(std::move(markerOps));

    // Lua 脚本库：目录可用 PS_SCRIPTS_DIR 覆盖（部署/测试），默认 scripts/ops。
    // 注册表注入：脚本算子登记进 AppContext 持有的 TransformHub 实例。
    const QByteArray dirOverride = qgetenv("PS_SCRIPTS_DIR");
    m_scriptLib = std::make_unique<nextpilot::plotscope::script::LuaScriptLibrary>(host.app().transforms());
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
    host.registerTransformScriptOps(ops);

    // op 目录：工作台速查面板的数据源。存 provider 实时拉取，与实际可执行意图一致
    // （handler 是各模块 initialize 陆续注册的，快照会漏后注册者）。
    host.app().interactions().registerOpCatalogue([hostPtr]() {
        return hostPtr->registeredOps();
    });

    // 工作台执行通道：GUI 面板（编辑器/控制台）经 viewAction 调用，引擎归模块——
    // GUI 层不直接依赖 script 模块内部类（层契约：ps_module_script_gui 禁止横向
    // 依赖 ps_module_script），输出回显经 script.console.append 回面板。
    registerWorkbenchActions(host, std::move(viewDispatcher));
}

void ScriptModule::registerWorkbenchActions(RuntimeHost &host, ScriptFacade::ViewActionDispatcher dispatcher)
{
    IIntentExecutor &executor = host;
    JobRecorder *recorder = host.recorder();

    // 输出回显有两条去向，互不替代：
    //   ① script.console.append —— 终端面板增量回显（GUI 面板注册的处理器）；
    //   ② AppContext 消息流 —— 输出/问题/调试控制台共用同一份数据。
    // 在 ② 之前，"输出""问题"面板只能去偷终端面板的私有 widget，那是错的方向。
    // headless 下两条都没有订阅者，append 只是入库，无副作用。
    nextpilot::plotscope::context::InteractionHub *inter = &host.app().interactions();
    nextpilot::plotscope::context::AppContext *app = &host.app();
    auto echo = [inter, app](const QString &text, bool error) {
        app->appendMessage(error ? QStringLiteral("error") : QStringLiteral("info"),
                           QStringLiteral("脚本"), text);
        QVariantMap p;
        p.insert(QStringLiteral("text"), text);
        p.insert(QStringLiteral("error"), error);
        Result<QVariant> out{QVariant()};
        (void)inter->dispatchViewAction(QStringLiteral("script.console.append"), p, out);
    };

    // script.workbench.run：运行编辑器当前文档。引擎每次新建（与 script.run 同路径）。
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.run"),
        [&executor, recorder, dispatcher, echo](const QVariantMap &params) -> Result<QVariant> {
            const QByteArray source = params.value(QStringLiteral("source")).toByteArray();
            const QString name = params.value(QStringLiteral("name")).toString();

            // .py 文档 → Python 子进程桥：与 Lua 同一个运行入口，语言按后缀路由。
            if (name.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
            {
                Intent py;
                py.op = QStringLiteral("script.py.exec");
                py.params.insert(QStringLiteral("source"),
                                 QString::fromUtf8(source));
                const Result<IntentResult> r = executor.execute(py);
                if (!r)
                {
                    echo(QStringLiteral("✘ Python: %1").arg(r.error().message), true);
                    return fail(r.error().code, r.error().message);
                }
                const QVariantMap out = r.value().payload.toMap();
                const QString printed = out.value(QStringLiteral("out")).toString();
                if (!printed.isEmpty())
                    echo(printed.trimmed(), false);
                const QVariant v = out.value(QStringLiteral("value"));
                if (v.isValid() && !v.isNull())
                    echo(QStringLiteral("⇒ %1").arg(v.toString()), false);
                return Result<QVariant>(QVariant(true));
            }

            ScriptFacade facade(executor, recorder, dispatcher);
            nextpilot::plotscope::script::LuaEngine engine(facade);
            engine.setOutputSink([&echo](const QString &m) { echo(m, false); });
            const Result<void> r = engine.runString(source, QStringLiteral("@%1").arg(name));
            if (!r)
                return fail(r.error().code, r.error().message);
            return Result<QVariant>(QVariant(true));
        });

    // script.workbench.repl：控制台求值。持久 VM 归模块持有，进程期共享全局状态。
    // 调试暂停期自动切换到调试会话的暂停期 REPL（新协程求值，同一 VM 全局表）。
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.repl"),
        [this, &executor, dispatcher, echo](const QVariantMap &params) -> Result<QVariant> {
            const QString code = params.value(QStringLiteral("code")).toString();
            if (m_dbgEngine && m_dbgEngine->debugPaused())
            {
                const Result<QVariant> r = m_dbgEngine->debugEvaluate(code);
                if (!r)
                    return fail(r.error().code, r.error().message);
                QVariant out;
                if (!r.value().isNull() && r.value().isValid())
                    out = r.value();
                return Result<QVariant>(out);
            }
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
        return m_ui->requestPanel(nextpilot::plotscope::registry::Region::Editor,
                                  QStringLiteral("script-editor")) != nullptr;
    };
    nextpilot::plotscope::context::InteractionHub *reg = &host.app().interactions();
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.open"),
        [this, ensureEditor, reg](const QVariantMap &params) -> Result<QVariant> {
            Result<QVariant> out{QVariant()};
            if (!ensureEditor(out))
                return out;
            reg->dispatchViewAction(QStringLiteral("script.open"), params, out);
            return out;
        });
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.new"),
        [this, ensureEditor, reg](const QVariantMap &params) -> Result<QVariant> {
            Result<QVariant> out{QVariant()};
            if (!ensureEditor(out))
                return out;
            reg->dispatchViewAction(QStringLiteral("script.new"), params, out);
            return out;
        });

    // ---- 调试通道（2.1.7-2.1.9）----

    // script.workbench.debug {source, name, breakpoints:[int]}：调试运行。
    // 引擎归模块（m_dbgEngine），暂停时上报快照（调试面板 + 编辑器行高亮），
    // 嵌套事件循环等控制指令（.continue/.stepover/...），结束后清快照。
    nextpilot::plotscope::context::InteractionHub *dbgReg = &host.app().interactions();
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.debug"),
        [this, &executor, recorder, dispatcher, echo, app, dbgReg](const QVariantMap &params) -> Result<QVariant> {
            const QByteArray source = params.value(QStringLiteral("source")).toByteArray();
            const QString name = params.value(QStringLiteral("name")).toString();
            QSet<int> breakpoints;
            const QVariantList bpList = params.value(QStringLiteral("breakpoints")).toList();
            for (const QVariant &v : bpList)
                breakpoints.insert(v.toInt());

            ScriptFacade facade(executor, recorder, dispatcher);
            auto engine = std::make_unique<nextpilot::plotscope::script::LuaEngine>(facade);
            engine->setOutputSink([&echo](const QString &m) { echo(m, false); });

            m_dbgEngine = std::move(engine);
            const Result<void> r = m_dbgEngine->runStringDebug(
                source, QStringLiteral("@%1").arg(name), breakpoints,
                [this, app, name, dbgReg](const nextpilot::plotscope::script::LuaEngine::DebugPause &p) {
                    // 调试事件同时进消息流：底部「调试控制台」按 level=debug 取它，
                    // 于是它和终端是两个视图看同一份数据，而不是两套私有输出。
                    app->appendMessage(QStringLiteral("debug"), QStringLiteral("调试"),
                                       QStringLiteral("暂停 @ %1:%2（%3）")
                                           .arg(name, QString::number(p.line), p.reason),
                                       QStringLiteral("%1:%2").arg(name).arg(p.line));
                    // 快照 → 调试面板（stack+var 树）
                    QVariantMap up;
                    up.insert(QStringLiteral("paused"), true);
                    up.insert(QStringLiteral("name"), name);
                    up.insert(QStringLiteral("line"), p.line);
                    up.insert(QStringLiteral("reason"), p.reason);
                    up.insert(QStringLiteral("locals"), p.locals);
                    QVariantList stack;
                    for (const QString &frame : p.stack)
                        stack.append(frame);
                    up.insert(QStringLiteral("stack"), stack);
                    Result<QVariant> out{QVariant()};
                    dbgReg->dispatchViewAction(QStringLiteral("script.debug.update"), up, out);
                    // 暂停时把调试面板顶到前台（re-request 复用 = 聚焦语义）；
                    // 否则 dock.right 上并列的属性页签会挡住变量/调用栈。
                    if (m_ui)
                        m_ui->requestPanel(nextpilot::plotscope::registry::Region::SecondarySidebar,
                                           QStringLiteral("script-debug"));
                    // 行号 → 编辑器（暂停行高亮跳转；update 的处理器归调试面板，不冲突）
                    QVariantMap pp;
                    pp.insert(QStringLiteral("name"), name);
                    pp.insert(QStringLiteral("line"), p.line);
                    Result<QVariant> out2{QVariant()};
                    dbgReg->dispatchViewAction(QStringLiteral("script.debug.pause"), pp, out2);
                },
                breakpoints.isEmpty()); // 有断点直接跑到首个断点；无断点才停在入口
            // 收尾：清调试面板快照 + 编辑器解除暂停高亮
            app->appendMessage(QStringLiteral("debug"), QStringLiteral("调试"),
                               QStringLiteral("调试结束"));
            Result<QVariant> co{QVariant()};
            dbgReg->dispatchViewAction(QStringLiteral("script.debug.clear"), QVariantMap(), co);
            Result<QVariant> done{QVariant()};
            dbgReg->dispatchViewAction(QStringLiteral("script.debug.done"), QVariantMap(), done);
            m_dbgEngine.reset();
            if (!r)
                return fail(r.error().code, r.error().message);
            return Result<QVariant>(QVariant(true));
        });

    // 控制通道：调试暂停期由编辑器工具条/快捷键触发。无会话/未暂停时明确报错。
    struct DebugCmd { const char *id; void (nextpilot::plotscope::script::LuaEngine::*fn)(); };
    static const DebugCmd kDebugCmds[] = {
        {"script.workbench.debug.continue", &nextpilot::plotscope::script::LuaEngine::debugContinue},
        {"script.workbench.debug.stepover", &nextpilot::plotscope::script::LuaEngine::debugStepOver},
        {"script.workbench.debug.stepin",   &nextpilot::plotscope::script::LuaEngine::debugStepIn},
        {"script.workbench.debug.stepout",  &nextpilot::plotscope::script::LuaEngine::debugStepOut},
        {"script.workbench.debug.stop",     &nextpilot::plotscope::script::LuaEngine::debugStop},
    };
    for (const DebugCmd &cmd : kDebugCmds)
    {
        const QString id = QString::fromLatin1(cmd.id);
        const bool isStop = cmd.fn == &nextpilot::plotscope::script::LuaEngine::debugStop;
        host.app().interactions().registerViewAction(
            id, [this, fn = cmd.fn, isStop](const QVariantMap &) -> Result<QVariant> {
                if (!m_dbgEngine)
                    return fail(err::Args, QStringLiteral("没有进行中的调试会话"));
                if (!isStop && !m_dbgEngine->debugPaused())
                    return fail(err::Args, QStringLiteral("调试未处于暂停状态"));
                (m_dbgEngine.get()->*fn)();
                return Result<QVariant>(QVariant(true));
            });
    }

    // script.workbench.runCurrent {debug?}：执行编辑器**当前标签**（Ribbon"运行"与
    // 面板 F5 同路径）。转发给编辑器面板的 script.editor.run；headless/无面板时报错明确。
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.runCurrent"),
        [this, ensureEditor, reg](const QVariantMap &params) -> Result<QVariant> {
            Result<QVariant> out{QVariant()};
            if (!ensureEditor(out))
                return out;
            reg->dispatchViewAction(QStringLiteral("script.editor.run"), params, out);
            return out;
        });

    // script.workbench.debugRun：调试模式执行当前标签（Ribbon"调试运行"，带断点单步）。
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.debugRun"),
        [this, ensureEditor, reg](const QVariantMap &) -> Result<QVariant> {
            Result<QVariant> out{QVariant()};
            if (!ensureEditor(out))
                return out;
            QVariantMap p;
            p.insert(QStringLiteral("debug"), true);
            reg->dispatchViewAction(QStringLiteral("script.editor.run"), p, out);
            return out;
        });

    // script.workbench.console：确保控制台面板存在（常驻 dock.bottom，请求即置前）。
    host.app().interactions().registerViewAction(
        QStringLiteral("script.workbench.console"),
        [this](const QVariantMap &) -> Result<QVariant> {
            if (!m_ui)
                return fail(err::Unsupported, QStringLiteral("脚本控制台需要 GUI 环境"));
            return Result<QVariant>(
                QVariant(m_ui->requestPanel(nextpilot::plotscope::registry::Region::Panel,
                                            QStringLiteral("script-console")) != nullptr));
        });
}

ScriptModule::~ScriptModule() = default;

void ScriptModule::onAttach(IUiContext &ui)
{
    // GUI 环境下才走到这里（headless 时宿主不调用 onAttach）。
    //
    // 三个侧边停靠面板随 attach 挂上（与既有面板同侧自动并成页签，零外壳改动）；
    // **编辑器不在此创建**——模块激活顺序不定，编辑器抢先会占住中央 dock、
    // 把画布挤成页签。编辑器按需创建（首次打开/新建脚本时）：
    // script.workbench.open / .new 先 ensureEditor 再转发给编辑器面板。
    // 面板实现由视图层自注册（ScriptPanels.cpp 的 PS_REGISTER_PANEL_T），
    // 模块只发字符串 id，不依赖 Qt Widgets，headless 构建保持纯净。
    m_ui = &ui;

    // 本域 Ribbon 页/组自声明（D13）。
    ui.declareRibbonPage(QStringLiteral("ribbon.page.script"), QStringLiteral("脚本"), 4);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.script.file"), QStringLiteral("文件"),
                          QStringLiteral("ribbon.page.script"), 0);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.script.run"), QStringLiteral("运行"),
                          QStringLiteral("ribbon.page.script"), 1);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.script.batch"), QStringLiteral("批量"),
                          QStringLiteral("ribbon.page.script"), 2);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.script.console"), QStringLiteral("控制台"),
                          QStringLiteral("ribbon.page.script"), 3);

    ActivitySpec scriptActivity;
    scriptActivity.id = QStringLiteral("script");
    scriptActivity.label = QStringLiteral("脚本");
    scriptActivity.icon = QStringLiteral("script");
    scriptActivity.order = 1;
    scriptActivity.ribbonPage = QStringLiteral("ribbon.page.script");
    scriptActivity.panelTypes = {QStringLiteral("script-files")};
    scriptActivity.centralPanel = QStringLiteral("script-editor");
    ui.declareActivity(scriptActivity);
    ui.requestPanel(nextpilot::plotscope::registry::Region::PrimarySidebar,
                    QStringLiteral("script-files"));
    ui.requestPanel(nextpilot::plotscope::registry::Region::Panel,
                    QStringLiteral("script-console"));
    ui.requestPanel(nextpilot::plotscope::registry::Region::SecondarySidebar,
                    QStringLiteral("script-debug"));

    // ---- Ribbon"脚本"页签（2.1.10）----
    // 文件/运行/控制台三组动作。intentOp 留空：外壳按 **action id** 查 view action
    // （处理器都在 registerWorkbenchActions 注册），与脚本 app.view(id) 同一条通道；
    // 未命中才回退意图层——保证 Ribbon 按钮、面板快捷键、脚本三者同路径。
    using nextpilot::plotscope::registry::UiAction;
    // icon 必填：脚本页每个按钮都要有图标，避免只剩文字的空板观感。
    const auto addRibbon = [&ui](const QString &id, const QString &label, const QString &area,
                                 const QString &data, int order, const QString &icon,
                                 bool large = false) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.area = area;
        a.data = data; // 额外参数（JSON），经外壳透传给 view action
        a.order = order;
        a.icon = icon;
        a.large = large;
        ui.addAction(a);
    };

    // "文件"组：新建 / 打开（组由本模块 onAttach 自声明）
    addRibbon(QStringLiteral("script.workbench.new"), QStringLiteral("新建脚本"),
              QStringLiteral("ribbon.group.script.file"), QString(), 0, QStringLiteral("plus"));
    addRibbon(QStringLiteral("script.workbench.open"), QStringLiteral("打开脚本"),
              QStringLiteral("ribbon.group.script.file"), QString(), 1, QStringLiteral("file_open"));

    // "运行"组：运行当前标签（与面板 F5 同路径）/ 调试运行（带断点、可单步）
    addRibbon(QStringLiteral("script.workbench.runCurrent"), QStringLiteral("运行当前"),
              QStringLiteral("ribbon.group.script.run"), QString(), 0, QStringLiteral("play"), true);
    addRibbon(QStringLiteral("script.workbench.debugRun"), QStringLiteral("调试运行"),
              QStringLiteral("ribbon.group.script.run"), QString(), 1, QStringLiteral("script_debug"), true);
    // 选 .lua/.py 文件 → script.run 意图（外壳文件对话框特判）。
    {
        UiAction runFile;
        runFile.id = QStringLiteral("app.action.runScript");
        runFile.label = QStringLiteral("运行脚本");
        runFile.area = QStringLiteral("ribbon.group.script.run");
        runFile.intentOp = QStringLiteral("script.run");
        runFile.icon = QStringLiteral("play");
        runFile.shortcut = QStringLiteral("Ctrl+F5");
        runFile.order = 2;
        ui.addAction(runFile);
    }

    // "批量"组：目录级批量运行——选目录+脚本 → 进度 → 汇总表
    addRibbon(QStringLiteral("script.workbench.batch"), QStringLiteral("批量运行"),
              QStringLiteral("ribbon.group.script.batch"), QString(), 0, QStringLiteral("replay"), true);

    // "控制台"组：置前 REPL 控制台
    addRibbon(QStringLiteral("script.workbench.console"), QStringLiteral("控制台"),
              QStringLiteral("ribbon.group.script.console"), QString(), 0, QStringLiteral("terminal"));
}

#include "ScriptModule.moc"

#include "ErrorCodes.h"
// 编译期自注册：新增脚本模块无需改 App / 宿主。
PS_REGISTER_MODULE(ScriptModule)
