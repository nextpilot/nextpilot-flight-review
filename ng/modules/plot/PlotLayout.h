#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace nextpilot::plotscope::core
{

// 一个画布在布局中的声明。
//
// 声明式：只描述"这个画布是什么、放在哪、什么样式"，
// 不含任何指针或运行期状态 —— 于是布局可以存盘、分享、跨会话恢复。
struct CanvasSpec
{
    QString id;            // 画布稳定标识（用于恢复时匹配视图实例）
    QString viewType;      // 视图类型 id（"timeseries" / "spectrum" / ...）
    QString title;
    QString datasetId;     // 绑定哪个数据集（字符串化 id）
    QStringList channels;  // 绘制的通道（table.column）
    QJsonObject style;     // 曲线样式（颜色/线宽/线型/轴…），由视图自己解释
    QJsonObject state;     // 视图自己的状态（时间窗、缩放…）
};

// 布局树的一个节点：要么是画布，要么是子节点（拆分）。
struct LayoutNode
{
    bool isCanvas = false;
    CanvasSpec canvas;                 // isCanvas=true 时有效
    QVector<LayoutNode> children;      // isCanvas=false 时有效
    QString direction;                 // "horizontal" / "vertical"：子节点排列方向
};

// 一个 plot 页面：若干画布 + 页面级设置。
struct PlotPageSpec
{
    QString id;
    QString title;
    LayoutNode root;                   // 布局树（根节点通常是拆分）
    QJsonObject pageState;             // 页面级状态（当前画布、滚动位置…）
};

// 一个完整的 plot 工作区：多页 + 文档级设置。
//
// 这就是**文档作用域**的内容：可存盘、可分享、可跨会话恢复。
// 恢复时按 id 匹配已有资源（数据集/视图），匹配不上的安全跳过并记录。
struct PlotDocumentSpec
{
    int version = 1;
    QString name;
    QVector<PlotPageSpec> pages;
    QJsonObject documentState;         // 时间窗、书签等文档级状态

    // 序列化为 JSON（存进 SettingsStore 的 Document 作用域，或写成独立文件）。
    QJsonObject toJson() const;
    static PlotDocumentSpec fromJson(const QJsonObject &obj);
};

} // namespace nextpilot::plotscope::core
