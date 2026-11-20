#include "diagnostics.h"
#include "App.h"
#include "RuntimeHost.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QVersionNumber>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::host;
using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::app::diag
{

QJsonObject collect(App &application)
{
    QJsonObject root;

    // 应用与环境
    root[QStringLiteral("application")] = QJsonObject{
        { QStringLiteral("name"), QCoreApplication::applicationName() },
        { QStringLiteral("version"), QCoreApplication::applicationVersion() },
        { QStringLiteral("qt"), QString::fromLatin1(qVersion()) },
    };

    // 器官模块
    QJsonArray modules;
    for (const ModuleInfo &info : application.host().availableModules())
    {
        modules.append(QJsonObject{
            { QStringLiteral("id"), info.id },
            { QStringLiteral("kind"), info.kind == ModuleKind::Builtin ? QStringLiteral("builtin")
                                                                       : QStringLiteral("extension") },
            { QStringLiteral("provides"), QJsonArray::fromStringList(info.provides) },
        });
    }
    root[QStringLiteral("modules")] = modules;

    // 知识包
    QJsonArray packs;
    const auto &toolbox = application.host().extensions();
    for (const QString &packId : toolbox.packIds())
    {
        IToolPack *pack = toolbox.pack(packId);
        if (!pack)
            continue;
        packs.append(QJsonObject{
            { QStringLiteral("id"), pack->id() },
            { QStringLiteral("name"), pack->displayName() },
            { QStringLiteral("domain"), QString::fromLatin1(domainKey(pack->domain())) },
            { QStringLiteral("contributions"), static_cast<int>(pack->contributions().size()) },
        });
    }
    root[QStringLiteral("packs")] = packs;

    // 数据面：数据集/源数量（载入前为 0，诊断截屏常配合 --datafile 使用）
    root[QStringLiteral("data")] = QJsonObject{
        { QStringLiteral("datasets"), application.context().data().count() },
    };

    // AI 工具目录（MCP 桥/意图工具层的白名单面）
    const QJsonArray tools = AIToolsHandler::toolCatalog();
    root[QStringLiteral("aiTools")] = QJsonObject{
        { QStringLiteral("count"), tools.size() },
        { QStringLiteral("names"), [&] {
             QStringList names;
             for (const QJsonValue &v : tools)
                 names << v.toObject().value(QStringLiteral("name")).toString();
             return QJsonArray::fromStringList(names);
         }() },
    };

    return root;
}

} // namespace nextpilot::plotscope::app::diag
