#include "ScriptModule.h"

#include "BuiltinModuleRegistry.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "LuaEngine.h"
#include "ScriptFacade.h"

#include <QFile>

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::host;

// script.run 处理器：接受 {source=...} 或 {path=...}，组装门面并执行脚本。
// 门面绑定宿主执行器 + 可选录制器，因此脚本调用与 GUI 点击走同一条执行路径。
class ScriptModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(IIntentExecutor &executor, JobRecorder *recorder, QObject *parent)
        : QObject(parent), m_executor(executor), m_recorder(recorder) {}

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
                return fail(QStringLiteral("eio"), QStringLiteral("无法读取脚本: %1").arg(path));
            source = f.readAll();
        }
        else
        {
            return fail(QStringLiteral("eargs"), QStringLiteral("script.run 需要 source 或 path"));
        }

        // 门面是栈对象，仅本次执行有效；脚本拿不到它的指针。
        ScriptFacade facade(m_executor, m_recorder);
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
    m_handler = new Handler(ctx.executor(), ctx.recorder(), this);
    ctx.registerIntentHandler(m_handler);
}

#include "ScriptModule.moc"

// 编译期自注册：新增脚本模块无需改 App / 宿主。
NG_REGISTER_MODULE(ScriptModule)
