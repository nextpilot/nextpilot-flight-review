// MCP stdio 桥契约测试：直接调 mcp::handleRequest 纯函数（不经过 stdin/stdout）。
// 覆盖：initialize / ping / tools/list（schema 转换）/ tools/call 正常与工具失败 /
// 未知 method / notification 不回包 / 坏 JSON 错误码。
#include "App.h"
#include "mcp_bridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdio>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::mcp;

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond)                                                                                                \
    do                                                                                                             \
    {                                                                                                              \
        if (cond)                                                                                                  \
        {                                                                                                          \
            ++g_pass;                                                                                              \
        }                                                                                                          \
        else                                                                                                       \
        {                                                                                                          \
            ++g_fail;                                                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                        \
        }                                                                                                          \
    } while (0)
#define OK(label) fprintf(stdout, "[ok] %s\n", label)

static QJsonObject req(const QString &method, const QJsonValue &id, const QJsonObject &params = {})
{
    QJsonObject r;
    r[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    if (!id.isNull() || !id.isUndefined())
        r[QStringLiteral("id")] = id;
    r[QStringLiteral("method")] = method;
    if (!params.isEmpty())
        r[QStringLiteral("params")] = params;
    return r;
}

static QJsonObject call(const QJsonObject &request, App &application)
{
    const QJsonObject resp = handleRequest(request, application);
    CHECK(resp.value(QStringLiteral("jsonrpc")).toString() == QLatin1String("2.0"));
    return resp;
}

int main()
{
    App application;
    const auto errors = application.startup(/*headless=*/true);

    // 1. initialize：协议版本 + serverInfo + tools 能力。
    {
        const QJsonObject resp = call(req(QStringLiteral("initialize"), 1), application);
        const QJsonObject result = resp.value(QStringLiteral("result")).toObject();
        CHECK(result.value(QStringLiteral("protocolVersion")).toString() == QLatin1String("2024-11-05"));
        CHECK(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString()
              == QLatin1String("plotscope"));
        CHECK(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        OK("initialize");
    }

    // 2. ping。
    {
        const QJsonObject resp = call(req(QStringLiteral("ping"), 2), application);
        CHECK(resp.contains(QStringLiteral("result")));
        OK("ping");
    }

    // 3. tools/list：≥10 工具，schema 有 properties/required，inputSchema 类型映射正确。
    {
        const QJsonObject resp = call(req(QStringLiteral("tools/list"), 3), application);
        const QJsonArray tools = resp.value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("tools")).toArray();
        CHECK(tools.size() >= 10);
        bool foundReadSeries = false;
        for (const auto &tv : tools)
        {
            const QJsonObject t = tv.toObject();
            CHECK(!t.value(QStringLiteral("name")).toString().isEmpty());
            CHECK(t.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString()
                  == QLatin1String("object"));
            if (t.value(QStringLiteral("name")).toString() == QLatin1String("read_series"))
                foundReadSeries = true;
        }
        CHECK(foundReadSeries); // #215 新增的 read_series 必须在目录里
        OK("tools/list");
    }

    // 4. tools/call 正常路径：read_series 读 fixture 列（先 open_file 加载数据）。
    //    注意：有界读取经 kernel 抽稀，返回点数 ≤ limit（不恒等于），truncated 表达
    //    "rows > limit"。dataset id 从 open_file 的 result 取（data.load payload 是 id）。
    {
        QJsonObject openResp = call(req(QStringLiteral("tools/call"), 4,
                                        QJsonObject{ { QStringLiteral("name"), QStringLiteral("open_file") },
                                                     { QStringLiteral("arguments"),
                                                       QJsonObject{ { QStringLiteral("path"),
                                                                      QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv") } } } }),
                                    application);
        const QJsonObject openResult = openResp.value(QStringLiteral("result")).toObject();
        CHECK(!openResult.value(QStringLiteral("isError")).toBool());
        const QJsonObject openPayload =
            QJsonDocument::fromJson(openResult.value(QStringLiteral("content")).toArray().first().toObject()
                                        .value(QStringLiteral("text")).toString().toUtf8()).object();
        const QJsonValue dsId = openPayload.value(QStringLiteral("result"));

        QJsonObject resp = call(req(QStringLiteral("tools/call"), 5,
                                    QJsonObject{ { QStringLiteral("name"), QStringLiteral("read_series") },
                                                 { QStringLiteral("arguments"),
                                                   QJsonObject{ { QStringLiteral("dataset"), dsId },
                                                                { QStringLiteral("column"),
                                                                  QStringLiteral("battery_v[V]") },
                                                                { QStringLiteral("limit"), 64 } } } }),
                                application);
        const QJsonObject result = resp.value(QStringLiteral("result")).toObject();
        CHECK(!result.value(QStringLiteral("isError")).toBool());
        const QJsonObject payload =
            QJsonDocument::fromJson(result.value(QStringLiteral("content")).toArray().first().toObject()
                                        .value(QStringLiteral("text")).toString().toUtf8()).object();
        // ai.tool.call 的 payload 是 {tool, op, result:{data.series 的字段}}。
        const QJsonObject inner = payload.value(QStringLiteral("result")).toObject();
        const int count = inner.value(QStringLiteral("count")).toInt();
        CHECK(count >= 1 && count <= 64);
        CHECK(inner.value(QStringLiteral("t")).toArray().size() == count);
        CHECK(inner.value(QStringLiteral("v")).toArray().size() == count);
        CHECK(inner.value(QStringLiteral("truncated")).toBool()); // 3000 行 > 64
        OK("tools/call read_series");
    }

    // 5. tools/call 工具失败：走 isError 而非协议错误。
    {
        const QJsonObject resp = call(req(QStringLiteral("tools/call"), 6,
                                          QJsonObject{ { QStringLiteral("name"), QStringLiteral("unload_file") },
                                                       { QStringLiteral("arguments"),
                                                         QJsonObject{ { QStringLiteral("id"), 999 } } } }),
                                      application);
        const QJsonObject result = resp.value(QStringLiteral("result")).toObject();
        CHECK(result.value(QStringLiteral("isError")).toBool());
        CHECK(!resp.contains(QStringLiteral("error")));
        OK("tools/call isError");
    }

    // 6. 未知工具 → isError；白名单拒绝任意 op。
    {
        const QJsonObject resp = call(req(QStringLiteral("tools/call"), 7,
                                          QJsonObject{ { QStringLiteral("name"), QStringLiteral("kernel_selfcheck") } }),
                                      application);
        CHECK(resp.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        OK("tools/call 未知工具拒绝");
    }

    // 7. 未知 method → -32601。
    {
        const QJsonObject resp = call(req(QStringLiteral("resources/list"), 8), application);
        CHECK(resp.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt() == -32601);
        OK("method 未实现");
    }

    // 8. notification（无 id）不回包。
    {
        QJsonObject notify;
        notify[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        notify[QStringLiteral("method")] = QStringLiteral("notifications/initialized");
        const QJsonObject resp = handleRequest(notify, application);
        CHECK(resp.isEmpty());
        OK("notification 静默");
    }

    // 9. errorResponse 直构。
    {
        const QJsonObject resp = errorResponse(QJsonValue::Null, -32700, QStringLiteral("bad"));
        CHECK(resp.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt() == -32700);
        OK("errorResponse");
    }

    fprintf(stdout, "ps_test_mcp_bridge: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
