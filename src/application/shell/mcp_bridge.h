#pragma once

#include <QJsonObject>

namespace nextpilot::plotscope::app
{
class App;
}

namespace nextpilot::plotscope::mcp
{

// MCP stdio 桥：把应用经 ai.* 意图层暴露为 MCP (Model Context Protocol) 工具，
// 供外部代理（Python/任意语言的 MCP 客户端）headless 驱动——
// --mcp-bridge 路线。传输层（newline-delimited JSON-RPC 2.0 over stdin/stdout）
// 在 cli_main.cpp；本文件只做协议语义，纯函数、无 IO，便于单测。
//
// 支持的 method：
//   initialize                → 协议版本 + serverInfo
//   ping                      → {}
//   tools/list                → ai.tools.list 目录转 MCP inputSchema
//   tools/call                → ai.tool.call（白名单工具），执行错误走 isError
// 其余 method → JSON-RPC error -32601。notification（无 id）不回包。
QJsonObject handleRequest(const QJsonObject &req, app::App &application);

// 构造 JSON-RPC 错误响应（id 可为 null）。cli_main 解析失败时也用它回 -32700。
QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message);

} // namespace nextpilot::plotscope::mcp
