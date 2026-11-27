#include "SettingsModule.h"

#include "BuiltinModuleHub.h"
#include "IIntentExecutor.h"
#include "InstantiationService.h"
#include "IWorkspace.h"
#include "ICommands.h"
#include "CommandAdapter.h"
#include "Intent.h"
#include "RuntimeHost.h"

#include <cstdio>

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
// 意图参数里的 scope 名（app/workspace/document，便于脚本/CLI）→ SettingsStore 的存储前缀
// （app/ws/doc，同 SettingsStore.cpp scopePrefix）。扁平 key = 前缀 + "/" + 相对键。
QString scopeKeyPrefix(const QString &name)
{
    if (name.compare(QStringLiteral("workspace"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("ws");
    if (name.compare(QStringLiteral("document"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("doc");
    return QStringLiteral("app");
}

QString scopedKey(const QString &scopeName, const QString &relative)
{
    const QString prefix = scopeKeyPrefix(scopeName);
    return relative.isEmpty() ? prefix : prefix + QLatin1Char('/') + relative;
}
} // namespace

// settings 意图处理器：settings.get / settings.set / settings.list / settings.flush / settings.remove
//
// 这是"统一设置管理"的对外接口：GUI、CLI、脚本走同一条路径。
// 键的语义由调用方与各贡献方约定，本处理器不做任何领域判断。
// 边界：**不提供任何 UI 入口**——"打开设置对话框"是 GUI 专属意图
// （preferences.open），由外壳注册系统级 handler 处理；本模块只做存取，
// 这样 headless 里它永远不需要认识 QWidget。
class SettingsModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(nextpilot::plotscope::workbench::IWorkspace &ws, QObject *parent) : QObject(parent), m_ws(ws) {}

    bool canHandle(const Intent &intent) const override
    {
        return intent.op.startsWith(QStringLiteral("settings."));
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        const QString op = intent.op;

        if (op == QStringLiteral("settings.get"))
        {
            const QString key = intent.params.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                return fail(err::Key, QStringLiteral("缺少 key"));
            IntentResult r;
            r.payload = m_ws.getConfiguration(key);
            return r;
        }

        if (op == QStringLiteral("settings.set"))
        {
            const QString key = intent.params.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                return fail(err::Key, QStringLiteral("缺少 key"));
            m_ws.setConfiguration(key, intent.params.value(QStringLiteral("value")));
            IntentResult r;
            r.payload = true;
            return r;
        }

        if (op == QStringLiteral("settings.scoped.get"))
        {
            const QString scopeNameParam = intent.params.value(QStringLiteral("scope")).toString();
            const QString rel = intent.params.value(QStringLiteral("key")).toString();
            IntentResult r;
            r.payload = m_ws.getConfiguration(scopedKey(scopeNameParam, rel));
            return r;
        }

        if (op == QStringLiteral("settings.scoped.set"))
        {
            const QString scopeNameParam = intent.params.value(QStringLiteral("scope")).toString();
            const QString rel = intent.params.value(QStringLiteral("key")).toString();
            m_ws.setConfiguration(scopedKey(scopeNameParam, rel), intent.params.value(QStringLiteral("value")));
            IntentResult r;
            r.payload = true;
            return r;
        }

        if (op == QStringLiteral("settings.list"))
        {
            const QString scopeNameParam = intent.params.value(QStringLiteral("scope")).toString();
            const QString prefix =
                scopeNameParam.isEmpty() ? QString() : scopeKeyPrefix(scopeNameParam) + QLatin1Char('/');
            IntentResult r;
            r.payload = m_ws.configurationKeys(prefix);
            return r;
        }

        if (op == QStringLiteral("settings.remove"))
        {
            const QString key = intent.params.value(QStringLiteral("key")).toString();
            m_ws.removeConfiguration(key);
            IntentResult r;
            r.payload = true;
            return r;
        }

        if (op == QStringLiteral("settings.flush"))
        {
            QString err;
            if (!m_ws.flushConfiguration(&err))
                return fail(err::Io, err);
            IntentResult r;
            r.payload = true;
            return r;
        }

        return fail(err::Unsupported, QStringLiteral("未知设置操作: %1").arg(op));
    }

private:
    nextpilot::plotscope::workbench::IWorkspace &m_ws;
};

ModuleInfo SettingsModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.settings"),
        QStringLiteral("设置模块"),
        QStringLiteral("1.0"),
        {QStringLiteral("settings.store")},
        {}
    };
}

IWorkbenchContribution *SettingsModule::create() { return new SettingsModule(); }

ModuleInfo SettingsModule::info() const
{
    return moduleInfo();
}

void SettingsModule::initialize(InstantiationService &services)
{
    // 设置存储归宿主所有（GUI/CLI/脚本共用一份），模块只做门面：
    // 只登记存取类意图，不碰任何 UI（设置对话框属 GUI 外壳，见 preferences.open）。
    auto *ws = services.getService<nextpilot::plotscope::workbench::IWorkspace>(QStringLiteral("vscode.workspace"));
    auto *cmds = services.getService<nextpilot::plotscope::workbench::ICommands>(QStringLiteral("vscode.commands"));
    if (!ws || !cmds)
        return;
    m_handler = new Handler(*ws, this);
    m_cmds = cmds;
    auto reg = [this, cmds](const QString &id, nextpilot::plotscope::intent::IIntentHandler *h) {
        m_commandIds.append(id);
        cmds->registerCommand(id, nextpilot::plotscope::workbench::asCommandHandler(h, id));
    };
    // Handler::canHandle 用 startsWith("settings.")，execute 覆盖 get/set/scoped.get/
    // scoped.set/list/remove/flush 七个 op。asCommandHandler 每个 Bridge 只匹配单个
    // 固定 id，故七个 op 必须逐一注册；"settings.store" 是 provides 能力名不是 op。
    for (const QString &op :
         {QStringLiteral("settings.get"), QStringLiteral("settings.set"), QStringLiteral("settings.scoped.get"),
          QStringLiteral("settings.scoped.set"), QStringLiteral("settings.list"), QStringLiteral("settings.flush"),
          QStringLiteral("settings.remove")})
        reg(op, m_handler);
}

void SettingsModule::onDetach()
{
    if (m_cmds)
        for (const QString &id : m_commandIds)
            m_cmds->unregisterCommand(id);
    m_commandIds.clear();
}

#include "SettingsModule.moc"

#include "ErrorCodes.h"
// 编译期自注册：新增模块无需改 App / 宿主。
PS_REGISTER_MODULE(SettingsModule)
