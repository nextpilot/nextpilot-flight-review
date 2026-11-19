#pragma once

#include "Result.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace nextpilot::plotscope::uischema
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;

// 属性契约（P0-2）：把老 PropertyManager 里"既是模型又是控件"的东西撕成两半。
//
//   - 提供者（绘图画布、协议编辑器、数据集…）**声明 schema**：有哪些对象、
//     每个对象有哪些属性行、值是什么、哪些可编辑、编辑值是什么类型。
//   - 属性表**只是视图**：照 schema 显示，编辑时发 Intent(prop.set)，
//     不认识任何业务类型、不含任何 QWidget 概念。
//
// 为什么必须先有它：老代码里 plotting 的属性页与 protocol 编辑器**各自**
// 偷偷依赖同一个 PropertyManager（449 行胖 UI）。若搬两个 module 时不先定契约，
// ng 必然长出两份私有属性面板——这正是 AGENTS.md P0-2 标红的返工点。
//
// 本文件是**纯数据**（QJsonObject/QVariant，无 QWidget），core/headless 可见，
// 于是 CLI 也能查询"某对象有哪些属性"，GUI 只负责把它画成表格。

// 属性行的编辑类型。视图据此选控件（QLineEdit/QCheckBox/QComboBox/颜色按钮）。
enum class PropKind
{
    Text,    // 自由文本
    Number,  // 数值（含单位后缀）
    Bool,    // 布尔（复选框）
    Enum,    // 枚举，选项在 options
    Color,   // 颜色
    ReadOnly // 只读展示（如行数、类型）
};

// 一行属性：schema 的最小单元。
struct PropRow
{
    QString key;      // 稳定键（编辑时回传，提供者据此改值）
    QString label;    // 展示名
    QString value;    // 展示值（字符串化；编辑后用 parse 还原）
    PropKind kind = PropKind::ReadOnly;
    bool editable = false;
    QString unit;     // 数值单位（展示用，如 "deg" / "px"）
    QStringList options; // Enum 的候选项
    QString tooltip;
};

// 对象树里的一个可检查对象：一条属性记录 + 它挂在哪个父节点下。
struct PropObject
{
    QString id;          // 稳定标识（"canvas" / "series:0" / "protocol.frame"）
    QString parentId;    // 父对象 id，空 = 根
    QString label;       // 树节点展示名
    QString icon;        // 可选图标名（外壳据此取 QIcon）
    QVector<PropRow> rows;
};

// 一份完整属性文档：某个当前选中对象（如活动画布）暴露出来的属性树。
struct PropertySchema
{
    QString title;                    // 面板标题（如 "画布属性" / "demo_flight"）
    QVector<PropObject> objects;
    QString selectedId;               // 当前选中对象 id

    bool empty() const { return objects.isEmpty(); }
    const PropObject *object(const QString &id) const;

    QJsonObject toJson() const;
    static PropertySchema fromJson(const QJsonObject &obj);
};

// 意图参数约定（两边共用，避免各写一套键名）：
//   op = "prop.schema"   params: { "source": <提供者id>, ... }  -> payload: schema.toJson()
//   op = "prop.set"      params: { "source": <提供者id>, "object": <id>,
//                                  "key": <row.key>, "value": <新值字符串> }
//                       -> payload: true / 失败带 error
// "source" 区分不同提供者（"plot.canvas" / "protocol.editor"），
// 于是同一个 prop.set 意图能精确路由到正确的属性持有者，互不串台。
namespace PropIntent
{
inline const QString SchemaOp = QStringLiteral("prop.schema");
inline const QString SetOp = QStringLiteral("prop.set");
inline const QString SourceKey = QStringLiteral("source");
inline const QString ObjectKey = QStringLiteral("object");
inline const QString KeyKey = QStringLiteral("key");
inline const QString ValueKey = QStringLiteral("value");
} // namespace PropIntent

} // namespace nextpilot::plotscope::uischema
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using uischema::PropKind;
using uischema::PropRow;
using uischema::PropObject;
using uischema::PropertySchema;
} // namespace nextpilot::plotscope::core

