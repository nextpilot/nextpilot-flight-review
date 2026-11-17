#include "scripting/ScriptingModule.h"

#include "HostDataService.h"
#include "scripting/ConsolePanel.h"
#include "ScriptLibrary.h"

#include <QDir>
#include <QFileInfo>

namespace ps::components
{
namespace
{
struct ScriptingModuleRegistrar
{
    ScriptingModuleRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("script.core"), QStringLiteral("脚本子系统"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<ScriptingModule>(); });
    }
};
static ScriptingModuleRegistrar g_scriptingModuleRegistrar;

// 脚本库根目录下的 .lua 清单，供 script.info 自检
QStringList libraryFiles(const QString &root)
{
    QDir dir(root);
    if (!dir.exists())
        return {};
    return dir.entryList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
}
} // namespace

ComponentManifest ScriptingModule::manifest() const
{
    return {QStringLiteral("script.core"), QStringLiteral("脚本子系统"), QStringLiteral("1.0"),
            {QStringLiteral("script")}};
}

void ScriptingModule::contribute(Registry &registry, HostContext &host)
{
    // 控制台是脚本子系统的主入口，挂在底部停靠区
    registry.addDock(ps::scripting::ConsolePanel::dockContribution());

    // 脚本库放配置目录下：<configDir>/scripts/lib，与方案 §2.4 一致。
    if (host.configDir)
        ps::scripting::ScriptLibrary::setRoot(
            QDir(host.configDir()).filePath(QStringLiteral("scripts/lib")));

    // S0 只有引擎与受限 require；控制台 REPL、自动化 Job、事件分发在后续切片里长。
    if (host.registerCommand)
    {
        host.registerCommand(QStringLiteral("script.info"), [host](const QString &) {
            if (!host.log)
                return;
            const QString root = ps::scripting::ScriptLibrary::root();
            const QStringList files = libraryFiles(root);
            host.log(QStringLiteral("脚本库：%1（%2 个模块）")
                         .arg(root, QString::number(files.size())));
            if (files.isEmpty())
                host.log(QStringLiteral("脚本库为空或不可读；require 只认这个目录下的 .lua"));
            else
                host.log(QStringLiteral("模块：") + files.join(QStringLiteral(", ")));
            host.log(QStringLiteral("数据面服务：") +
                     (ps::host::hostService() ? QStringLiteral("已注册")
                                                   : QStringLiteral("未注册（脚本读数据会报 eneedhost）")));
        });
    }
}

} // namespace ps::components
