#include "SettingsModule.h"

#include "BuiltinModuleRegistry.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "ModuleHost.h"
#include "SettingsStore.h"

#include <cstdio>

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::host;

namespace
{
// scope 名 ↔ 枚举 的双向映射（意图参数用字符串，便于脚本/CLI 使用）
QString scopeName(SettingsScope scope)
{
    switch (scope)
    {
    case SettingsScope::App:       return QStringLiteral("app");
    case SettingsScope::Workspace: return QStringLiteral("workspace");
    case SettingsScope::Document:  return QStringLiteral("document");
    }
    return QStringLiteral("app");
}

SettingsScope scopeFromName(const QString &name)
{
    if (name.compare(QStringLiteral("workspace"), Qt::CaseInsensitive) == 0)
        return SettingsScope::Workspace;
    if (name.compare(QStringLiteral("document"), Qt::CaseInsensitive) == 0)
        return SettingsScope::Document;
    return SettingsScope::App;
}
} // namespace

// settings 意图处理器：settings.get / settings.set / settings.list / settings.flush / settings.remove
//
// 这是"统一设置管理"的对外接口：GUI、CLI、脚本走同一条路径。
// 键的语义由调用方与各贡献方约定，本处理器不做任何领域判断。
class SettingsModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(SettingsStore &store, QObject *parent)
        : QObject(parent), m_store(store) {}

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
                return fail(QStringLiteral("ekey"), QStringLiteral("缺少 key"));
            IntentResult r;
            r.payload = m_store.value(key);
            return r;
        }

        if (op == QStringLiteral("settings.set"))
        {
            const QString key = intent.params.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                return fail(QStringLiteral("ekey"), QStringLiteral("缺少 key"));
            m_store.setValue(key, intent.params.value(QStringLiteral("value")));
            IntentResult r;
            r.payload = true;
            return r;
        }

        if (op == QStringLiteral("settings.scoped.get"))
        {
            const SettingsScope scope = scopeFromName(
                intent.params.value(QStringLiteral("scope")).toString());
            const QString rel = intent.params.value(QStringLiteral("key")).toString();
            IntentResult r;
            r.payload = m_store.scopedValue(scope, rel);
            return r;
        }

        if (op == QStringLiteral("settings.scoped.set"))
        {
            const SettingsScope scope = scopeFromName(
                intent.params.value(QStringLiteral("scope")).toString());
            const QString rel = intent.params.value(QStringLiteral("key")).toString();
            m_store.setScopedValue(scope, rel, intent.params.value(QStringLiteral("value")));
            IntentResult r;
            r.payload = true;
            return r;
        }

        if (op == QStringLiteral("settings.list"))
        {
            QStringList keys;
            const QString scopeNameParam = intent.params.value(QStringLiteral("scope")).toString();
            if (scopeNameParam.isEmpty())
                keys = m_store.keys();
            else
                keys = m_store.keysIn(scopeFromName(scopeNameParam));
            IntentResult r;
            r.payload = keys;
            return r;
        }

        if (op == QStringLiteral("settings.remove"))
        {
            const QString key = intent.params.value(QStringLiteral("key")).toString();
            m_store.remove(key);
            IntentResult r;
            r.payload = true;
            return r;
        }

        if (op == QStringLiteral("settings.flush"))
        {
            QString err;
            if (!m_store.flush(&err))
                return fail(QStringLiteral("eio"), err);
            IntentResult r;
            r.payload = true;
            return r;
        }

        return fail(QStringLiteral("eunsupported"), QStringLiteral("未知设置操作: %1").arg(op));
    }

private:
    SettingsStore &m_store;
};

ModuleInfo SettingsModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.settings"),
        QStringLiteral("设置模块"),
        QStringLiteral("1.0"),
        ModuleKind::Builtin,
        {QStringLiteral("settings.store")},
        {}
    };
}

IModule *SettingsModule::create()
{
    return new SettingsModule();
}

ModuleInfo SettingsModule::info() const
{
    return moduleInfo();
}

void SettingsModule::initialize(ModuleContext &ctx)
{
    // 设置存储归宿主所有（GUI/CLI/脚本共用一份），器官只做门面。
    m_handler = new Handler(ctx.app().settings(), this);
    ctx.registerIntentHandler(m_handler);
}

#include "SettingsModule.moc"

// 编译期自注册：新增器官无需改 App / 宿主。
NG_REGISTER_MODULE(SettingsModule)
