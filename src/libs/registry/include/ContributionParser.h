#pragma once

// A8b 贡献契约：把一个 owner 的 `contributes` 文档（VSCode package.json 同名字段）
// 解析成 ContributesDoc 数据表。术语与键名一律照 VSCode，不另造模型。
//
// 设计约束（IKXXJ2 §3.0.14）：
//   - 单次解析、无副作用、不崩：未知键 → issue 跳过；类型不符 → issue 且该条丢弃；
//     重复 id → 后到者跳过 + issue（先到者保留，不静默覆盖）。
//   - M1 只认 `commands` + `menus.commandPalette`；M2 补齐 `viewsContainers` / `views` /
//     `menus` 其余 5 个 place / `colors` / `statusbar`；M3 解锁 `customEditors`/`configuration`。
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

// contributes.menus 其余 place[]（§3.0.1 固定枚举）：
//   editor/context | view/title | view/item/context | menubar/<pageId> | statusbar
// 条目 {command, group, when}；label/icon/order 为本仓呈现扩展（VSCode 用命令 title）。
// place 存原串；menubar/<pageId> 的 pageId 由外壳按 place 查页，不解析业务 id。
struct ContributedMenuItem
{
    QString place;
    QString command;
    QString group;
    QString when;
    QString label;
    QString icon;
    int order = 0;
};

// contributes.viewsContainers[] —— VSCode 视图容器（activity bar 入口）。
// area 只认 sidebar/bottom/auxiliary（§3.0.12 中央区只接受编辑器）；
// 本仓 ribbon 页签也经此声明（area="ribbon"），因为它是容器不是内容。
struct ContributedViewContainer
{
    QString id;    // 全局唯一
    QString title;
    QString area;  // sidebar | bottom | auxiliary | ribbon
    QString icon;
    int order = 0;
    QString owner; // 声明者归属（parse 不填；ViewRegistry.declare 时写）
};

// contributes.views[] —— 视图（面板），container 指向 viewsContainers[].id。
struct ContributedView
{
    QString id;        // 全局唯一
    QString name;
    QString container; // 所属 viewsContainers[].id
    QString when;
    int order = 0;
    QString owner;     // 声明者归属（parse 不填；ViewRegistry.declare 时写）
};

// contributes.colors[] —— 语义色 token（原 IUiContext 主题 token 声明迁此）。
struct ContributedColor
{
    QString id;    // 语义名，如 "plot.cursor"
    QString dark;  // #RRGGBB
    QString light; // #RRGGBB
};

// contributes.statusbar[] —— 状态栏项声明（原状态栏声明迁此）。
struct ContributedStatus
{
    QString id;       // 全局唯一
    QString side;     // left | right
    int priority = 0;
    QString when;
    QString icon;
    QString tooltip;
    QString commandId;
    QString text;
};

// contributes.customEditors[] —— VSCode 自定义编辑器声明。
// viewType 全局唯一（先 parser 内去重、跨 owner 由 ToolboxHub mount 诊断）；
// selector 按 mime 或 filenamePattern 匹配资源，壳据此选编辑器，不解释业务。
struct ContributedEditorSelector
{
    QString mime;            // 如 "application/x-pschema"；与 filenamePattern 二选一
    QString filenamePattern; // 如 "*.pschema"（简单 glob）
};

struct ContributedCustomEditor
{
    QString viewType;      // 全局唯一，registerEditorProvider 的对位键
    QString displayName;
    QList<ContributedEditorSelector> selector;
    int priority = 0;      // 多个编辑器都能打开时，大者优先
    QString owner;         // 声明者归属（parse 不填；ViewRegistry.declare 时写）
};

// contributes.configuration[] —— 设置属性声明（替换 SettingsPageSpec）。
// key 是全局扁平点号键（如 "plot.lineWidth"）；scope 缺省 window（VSCode ConfigurationScope
// 六值域）；字段白名单 §4.1，禁 tab/section/horizontal 布局节点——排版归工作台（§5）。
struct ConfigurationProperty
{
    QString key;  // 全局唯一点号键（parser 内去重，跨 owner 由 ToolboxHub mount 诊断）
    QString type; // boolean | number | string | object | array（enum 用 enumValues 表达）
    QJsonValue defaultValue;
    QStringList enumValues;
    QStringList enumDescriptions;
    double minimum = 0.0; // hasMinimum/hasMaximum 区分「未给」与「给了 0」（数值控件用）
    bool hasMinimum = false;
    double maximum = 0.0;
    bool hasMaximum = false;
    QString pattern;
    QString description;
    QString markdownDescription;
    QString scope; // VSCode ConfigurationScope 六值（缺省 window），参与合并分层
    int order = 0;
    QString editPresentation; // "multiline" → 多行控件（§5）
    QString category;         // §3.0.7：设置目录分组标题
};

// 一组设置（VSCode contributes.configuration 的一个条目）：设置目录按 (order, title) 排。
struct ConfigurationSpec
{
    QString title;
    QString iconId;
    int order = 0;
    QList<ConfigurationProperty> properties; // 只留整条校验通过的属性
};

// 一个 owner 解析后的 contributes 表（全文唯一名）。
struct ContributesDoc
{
    QString owner;
    QList<ContributedCommand> commands;
    QList<CommandPaletteMenuItem> commandPalette;

    // M2 新增（M1 恒为空，向后兼容）。
    QList<ContributedViewContainer> viewContainers;
    QList<ContributedView> views;
    QList<ContributedMenuItem> menuItems;
    QList<ContributedColor> colors;
    QList<ContributedStatus> statuses;

    // M3-a 新增（M2 恒为空，向后兼容）。
    QList<ContributedCustomEditor> customEditors;

    // M3-b 新增（M3-a 恒为空，向后兼容）。
    QList<ConfigurationSpec> configurations;

    bool isEmpty() const
    {
        return commands.isEmpty() && commandPalette.isEmpty() && viewContainers.isEmpty() && views.isEmpty() &&
               menuItems.isEmpty() && colors.isEmpty() && statuses.isEmpty() && customEditors.isEmpty() &&
               configurations.isEmpty();
    }
};

// 一条诊断：keyPath 如 "menus.view/title[2]" / "commands[1].command"。
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
