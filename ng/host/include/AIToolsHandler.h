#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"

#include <QJsonArray>
#include <QJsonObject>

#include <functional>

// AI/MCP 工具层（对齐 PJ4 appTools：把宿主能力暴露为机器可调用工具）。
// 本类只做"意图层能力暴露 + 工具目录"，MCP server 本身在第三梯队暂缓——
// 后续 MCP bridge 直接消费 ai.tools.list / ai.tool.call 即可，无需改宿主。
//
// 系统级 handler，归 RuntimeHost 持有；所有 ai.* 意图集中一处。
class AIToolsHandler final : public nextpilot::plotscope::intent::IIntentHandler
{
public:
    AIToolsHandler(nextpilot::plotscope::context::AppContext &appCtx,
                   nextpilot::plotscope::intent::IIntentExecutor &executor,
                   std::function<QStringList()> registeredOps);

    bool canHandle(const nextpilot::plotscope::intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> execute(
        const nextpilot::plotscope::intent::Intent &intent) override;

    // 工具目录：PJ4 工具名 -> NG op 的映射 + 参数 schema（静态数据，headless 可见）。
    static QJsonArray toolCatalog();

private:
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> listTools();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> callTool(
        const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> getStatus();

    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    std::function<QStringList()> m_registeredOps;
};
