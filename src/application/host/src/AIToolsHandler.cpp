#include "AIToolsHandler.h"

#include "AppContext.h"
#include "Intent.h"
#include "Result.h"
#include "ErrorCodes.h"

#include <QJsonDocument>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

// 工具目录：映射到 NG 的意图 op。
// 每个工具带 name/description/op/params（JSON schema 风格的简化参数说明），
// 供 AI/后续 MCP bridge 直接读取并构造 ai.tool.call 调用。
namespace
{
struct ToolDef
{
    const char *name;
    const char *description;
    const char *op;
    const char *paramsJson; // [{name,type,required,description}, ...]
};

const ToolDef kTools[] = {
    {"get_status", "查询应用当前状态（数据集数、源数、活动画布、模块列表）",
     "ai.status", "[]"},
    {"open_file", "加载一个本地数据文件（csv/ulog）成为数据源",
     "data.load", "[{\"name\":\"path\",\"type\":\"string\",\"required\":true,\"description\":\"文件路径\"},"
                  "{\"name\":\"format\",\"type\":\"string\",\"required\":false,\"description\":\"格式，缺省自动探测\"}]"},
    {"reload_file", "按源 id 重新加载数据源",
     "data.reload", "[{\"name\":\"id\",\"type\":\"int\",\"required\":true,\"description\":\"数据源 id\"}]"},
    {"unload_file", "移除一个数据源及其产出的数据集",
     "data.remove", "[{\"name\":\"id\",\"type\":\"int\",\"required\":true,\"description\":\"数据源 id\"}]"},
    {"add_script_series", "用 Lua 变换/Lua 脚本表达式生成一条只读派生序列",
     "data.derive", "[{\"name\":\"dataset\",\"type\":\"int\",\"required\":true,\"description\":\"数据集 id\"},"
                    "{\"name\":\"table\",\"type\":\"string\",\"required\":true,\"description\":\"表名\"},"
                    "{\"name\":\"column\",\"type\":\"string\",\"required\":true,\"description\":\"列名\"},"
                    "{\"name\":\"transform\",\"type\":\"object\",\"required\":true,\"description\":\"变换请求 JSON\"}]"},
    {"add_filter", "对一条通道应用有状态 Lua 过滤器（12 内置）",
     "data.filter", "[{\"name\":\"dataset\",\"type\":\"int\",\"required\":true,\"description\":\"数据集 id\"},"
                    "{\"name\":\"table\",\"type\":\"string\",\"required\":true,\"description\":\"表名\"},"
                    "{\"name\":\"column\",\"type\":\"string\",\"required\":true,\"description\":\"列名\"},"
                    "{\"name\":\"filter\",\"type\":\"string\",\"required\":true,\"description\":\"过滤器 id\"},"
                    "{\"name\":\"params\",\"type\":\"object\",\"required\":false,\"description\":\"过滤器参数\"}]"},
    {"add_marker", "运行 Luau Marker 规则，在画布上生成竖线/区间带/值带/事件点",
     "data.marker", "[{\"name\":\"dataset\",\"type\":\"int\",\"required\":true,\"description\":\"数据集 id\"},"
                    "{\"name\":\"table\",\"type\":\"string\",\"required\":true,\"description\":\"表名\"},"
                    "{\"name\":\"column\",\"type\":\"string\",\"required\":true,\"description\":\"列名\"},"
                    "{\"name\":\"code\",\"type\":\"string\",\"required\":true,\"description\":\"Luau 规则源码\"}]"},
    {"export_series", "导出通道数据为 CSV",
     "data.export", "[{\"name\":\"dataset\",\"type\":\"int\",\"required\":true,\"description\":\"数据集 id\"},"
                    "{\"name\":\"table\",\"type\":\"string\",\"required\":true,\"description\":\"表名\"},"
                    "{\"name\":\"column\",\"type\":\"string\",\"required\":true,\"description\":\"列名\"},"
                    "{\"name\":\"path\",\"type\":\"string\",\"required\":true,\"description\":\"输出路径\"}]"},
    {"read_series", "读取一条通道的 t/v 数值序列（脚本/代理做统计与条件判断的数据出口）",
     "data.series", "[{\"name\":\"dataset\",\"type\":\"int\",\"required\":false,\"description\":\"数据集 id，缺省取唯一数据集\"},"
                    "{\"name\":\"table\",\"type\":\"string\",\"required\":false,\"description\":\"表名，缺省取唯一表\"},"
                    "{\"name\":\"column\",\"type\":\"string\",\"required\":true,\"description\":\"列名\"},"
                    "{\"name\":\"limit\",\"type\":\"int\",\"required\":false,\"description\":\"最大样本数，缺省 4096\"}]"},
    {"script_run", "执行一段 Lua 脚本（app 全局可操作宿主，VBA 式自动化）",
     "script.run", "[{\"name\":\"source\",\"type\":\"string\",\"required\":true,\"description\":\"Lua 源码\"},"
                   "{\"name\":\"path\",\"type\":\"string\",\"required\":false,\"description\":\"脚本文件路径\"}]"},
    {"screenshot", "导出当前活动画布为 PNG",
     "plot.export.png", "[{\"name\":\"path\",\"type\":\"string\",\"required\":true,\"description\":\"输出 png 路径\"}]"},
};

QJsonArray parseParams(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).array();
}
} // namespace

AIToolsHandler::AIToolsHandler(AppContext &appCtx, IIntentExecutor &executor,
                               std::function<QStringList()> registeredOps)
    : m_app(appCtx), m_executor(executor), m_registeredOps(std::move(registeredOps))
{
}

QJsonArray AIToolsHandler::toolCatalog()
{
    QJsonArray catalog;
    for (const ToolDef &t : kTools)
    {
        QJsonObject o;
        o.insert(QStringLiteral("name"), QString::fromUtf8(t.name));
        o.insert(QStringLiteral("description"), QString::fromUtf8(t.description));
        o.insert(QStringLiteral("op"), QString::fromUtf8(t.op));
        o.insert(QStringLiteral("parameters"), parseParams(t.paramsJson));
        catalog.append(o);
    }
    return catalog;
}

bool AIToolsHandler::canHandle(const Intent &intent) const
{
    const QString &op = intent.op;
    return op == QLatin1String("ai.tools.list") || op == QLatin1String("ai.tool.call")
        || op == QLatin1String("ai.status");
}

Result<IntentResult> AIToolsHandler::execute(const Intent &intent)
{
    if (intent.op == QLatin1String("ai.tools.list"))
        return listTools();
    if (intent.op == QLatin1String("ai.tool.call"))
        return callTool(intent);
    if (intent.op == QLatin1String("ai.status"))
        return getStatus();
    return fail(err::Unknown,
                QStringLiteral("AIToolsHandler 不处理: %1").arg(intent.op));
}

Result<IntentResult> AIToolsHandler::listTools()
{
    const QJsonArray catalog = toolCatalog();
    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("tools"), catalog.toVariantList()},
        {QStringLiteral("count"), catalog.size()},
        {QStringLiteral("registeredOps"), QVariant(m_registeredOps ? m_registeredOps() : QStringList{})},
    };
    return r;
}

Result<IntentResult> AIToolsHandler::callTool(const Intent &intent)
{
    const QString tool = intent.params.value(QStringLiteral("tool")).toString();
    if (tool.isEmpty())
        return fail(err::Args, QStringLiteral("ai.tool.call 需要 tool 参数"));

    // 白名单：只接受工具目录里的名字，杜绝任意 op 透传。
    QString op;
    for (const ToolDef &t : kTools)
    {
        if (tool == QString::fromUtf8(t.name))
        {
            op = QString::fromUtf8(t.op);
            break;
        }
    }
    if (op.isEmpty())
        return fail(err::NoTool, QStringLiteral("未知工具: %1").arg(tool));

    // args 支持 QVariantMap 或 JSON 字符串（CLI/AI 两种来路）。
    QVariantMap args;
    const QVariant av = intent.params.value(QStringLiteral("args"));
    if (av.type() == QVariant::String)
    {
        const QJsonObject ao = QJsonDocument::fromJson(av.toString().toUtf8()).object();
        args = ao.toVariantMap();
    }
    else if (av.isValid() && !av.toMap().isEmpty())
        args = av.toMap();

    Intent target;
    target.op = op;
    target.params = args;
    const auto res = m_executor.execute(target);
    if (!res)
        return fail(res.error().code, res.error().message);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("tool"), tool},
        {QStringLiteral("op"), op},
        {QStringLiteral("result"), res.value().payload},
    };
    return r;
}

Result<IntentResult> AIToolsHandler::getStatus()
{
    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("datasets"), m_app.data().count()},
        {QStringLiteral("sources"), m_app.sources().count()},
        {QStringLiteral("activeSource"), m_app.activeSourceId()},
        {QStringLiteral("toolCount"), toolCatalog().size()},
    };
    return r;
}
