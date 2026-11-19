#pragma once

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// 贡献类型（kind）是**开放**标识：用 QString 而不是枚举。
//
// 这是工具箱 vs "格式驱动集合"的根本区别：
//   - 领域（Domain）是稳定的分类学：通用/飞控/汽车/机器人/卫导/视频，几乎不变；
//   - 贡献类型会随业务不断生长：格式、UI、脚本、数据、例子、文档……以及未来的
//     "算子 / 导出器 / 协议 / 主题"。若封成枚举，每加一类就要改 core 并全量重编译，
//     违反开闭原则。用字符串标识后，新增类型 = 新接口 + 注册槽，宿主与器官零改动。
//
// 下面只是**内置约定名**，外部插件可自定义任意 kind 字符串。
namespace ContributionKind
{
inline const QString Format  = QStringLiteral("format");   // IMessageParser：文件 -> Dataset
inline const QString Ui      = QStringLiteral("ui");       // IUiContribution：菜单/面板/dock/画布工具
inline const QString Panel   = QStringLiteral("panel");    // IPanelContribution：领域专属面板（形态随领域变）
inline const QString Script  = QStringLiteral("script");   // IScriptContribution：可复用脚本
inline const QString Data    = QStringLiteral("data");     // IDataContribution：样例数据/数据集
inline const QString Example = QStringLiteral("example");  // IExampleContribution：示例工程/demo
inline const QString Doc     = QStringLiteral("doc");      // IDocContribution：规格/知识文档
inline const QString Message = QStringLiteral("message");  // IMessageParser：协议消息解析/检查（产 DecodedFrame）
} // namespace ContributionKind

// 已知 kind 的中文展示名；未知 kind 原样返回（开放扩展友好）。
QString kindName(const QString &kind);

// 所有已登记过的内置 kind（按顺序，供 CLI 展示骨架）。
QStringList knownKinds();

} // namespace nextpilot::plotscope::registry
