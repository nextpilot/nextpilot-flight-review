#pragma once

// A8b-M1 贡献契约：把一个 owner 的 `contributes` 文档（VSCode package.json 同名字段）
// 解析成 ContributesDoc 数据表。术语与键名一律照 VSCode，不另造模型。
//
// 设计约束（IKXXJ2 §3.0.14）：
//   - 单次解析、无副作用、不崩：未知键 → issue 跳过；类型不符 → issue 且该条丢弃；
//     重复 id → 后到者跳过 + issue（先到者保留，不静默覆盖）。
//   - M1 阶段只认 `commands` 与 `menus.commandPalette` 两键；其余 place / 键由 M2/M3 解锁。
//   - registry 不认识命令库：when / group 只存字符串，求值与排序归命令层与外壳。
//   - 全文唯一名 ContributesDoc（不再出现 ContributionDoc）。

#include <QJsonObject>
#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// contributes.commands[] —— 键名照 VSCode（command/title/category/icon），order 为本仓排序扩展。
struct ContributedCommand
{
    QString command;   // 全局唯一 id
    QString title;
    QString category; // 命令面板分组标题
    QString icon;     // iconId；未注册用占位图标（M3 的 icons 键落地后校验）
    int order = 0;
};

// contributes.menus.commandPalette[] —— 条目 {command, group, when}。
struct CommandPaletteMenuItem
{
    QString command; // 指向 commands[].command
    QString group;   // "group@N"，排序归外壳
    QString when;    // 上下文表达式，只存串，求值归命令层
};

// 一个 owner 解析后的 contributes 表（全文唯一名）。
struct ContributesDoc
{
    QString owner;
    QList<ContributedCommand> commands;
    QList<CommandPaletteMenuItem> commandPalette;

    bool isEmpty() const { return commands.isEmpty() && commandPalette.isEmpty(); }
};

// 一条诊断：keyPath 如 "menus.commandPalette[2]" / "commands[1].command"。
struct ParseIssue
{
    QString keyPath;
    QString message;
};

struct ParseResult
{
    bool ok = true; // 无 issue 才为 true
    QList<ParseIssue> issues;
    ContributesDoc doc;
};

// 单次解析、无副作用。contributes 为已取出的 contributes 对象，owner 为声明者 id。
ParseResult parseContributes(const QJsonObject &contributes, const QString &owner);

} // namespace nextpilot::plotscope::registry
