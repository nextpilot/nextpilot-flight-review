#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <cstdint>

namespace nextpilot::plotscope::core
{

// 统一选中模型（P0）。
//
// 老架构的痛点：plot.selection.changed 只带一条曲线，属性面板拿不到
// "当前选中的到底是源 / 表 / 列 / 卡片 / 轴 / 曲线 / 协议字段"这层信息，
// 于是每个视图各存一份私有 selection，属性面板无法通用。
//
// ng 的收敛：全局只有**一个**当前选中对象（Selection），它是纯数据
// （QVariantMap 载荷，不含 QWidget），因此 core/headless 可见、脚本可读、
// 跨前端共享。视图点击时 setSelection，属性面板订阅 selectionChanged 后
// 按 kind 找对应 provider 拿 PropertySchema——星形结构，视图之间不互相认识。
//
// 统一 topic 取代旧的 plot.selection.changed（后者保留为兼容别名）。

enum class SelectionKind : uint8_t
{
    None = 0,
    Source,        // 数据源（顶层源行 / 卡片）
    Dataset,       // 数据集
    Table,         // 表
    Column,        // 列（字段，可画图）
    Param,         // 参数（键值表项）
    Message,       // 消息 / 话题
    Series,        // 画布上的一条曲线
    Axis,          // 坐标轴（左/右 Y 或 X）
    ProtocolField, // 协议字段（checksum/段/变体）
    Canvas,        // 画布本身（视图状态）
};

QString selectionKindName(SelectionKind kind);
SelectionKind selectionKindFromName(const QString &name);

// 一个选中对象的完整描述。ids 里放稳定标识，extra 放种类特有的补充信息，
// 例如：
//   Column   -> ids: sourceId/datasetId/table/column
//   Series   -> ids: seriesKey + datasetId/table/column，extra: color/axis
//   Axis     -> ids: axis = "x"|"y-left"|"y-right"
//   Protocol -> ids: field path，extra: checksum 类型
struct Selection
{
    SelectionKind kind = SelectionKind::None;
    QVariantMap ids;   // 稳定标识（source/dataset/table/column/seriesKey/axis/field）
    QString label;     // 展示名（属性面板标题）
    QString icon;      // 可选图标名
    QVariantMap extra; // 种类特有的补充信息

    bool empty() const { return kind == SelectionKind::None; }
    void clear();

    // 稳定字符串：供脚本/日志引用，也用于判断"是否换了选中对象"。
    QString key() const;

    QJsonObject toJson() const;
    static Selection fromJson(const QJsonObject &obj);
};

namespace topics
{
// 统一选中变化事件（取代 plot.selection.changed）。
inline const QString kSelectionChanged = QStringLiteral("core.selection.changed");
// 兼容别名：旧主题常量仍指向原字符串，二者由 SelectionModel 同时发。
inline const QString kSeriesSelectionChangedAlias = QStringLiteral("plot.selection.changed");
} // namespace topics

} // namespace nextpilot::plotscope::core
