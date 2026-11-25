#pragma once

// A8b-M2 裁定 52442006 Step 1：壳读/写槽位表改走 workbench::ISlotsService，
// src/application/gui 不再 include UiSlotHub.h——本头只承载**纯数据类型**
// （SlotKind/UiSlot/BoundUiAction），从 UiSlotHub.h 抽出供契约头与壳共用；
// UiSlotHub 仍是唯一实现，include 本头不改变任何行为。

#include "Contribution.h" // UiAction

#include <QString>

namespace nextpilot::plotscope::registry
{

// 插槽的语义类别：决定 GUI 层把内容放到哪里、怎么呈现。
//
// 插槽注册表只承载**有真实消费者**的容器：Ribbon 页签与分组。区域
// （ActivityBar / PrimarySidebar / SecondarySidebar / Panel / Editor）是壳的
// 代码常量，见 `Region.h`，不进本注册表（D13 / D17）。
enum class SlotKind
{
    RibbonPage,  // ribbon 的一个页签（下面再分组）
    RibbonGroup, // ribbon 页签内的一个分组
};

// 一个 UI 插槽：由壳或模块声明（容器），由模块或工具箱贡献填充（内容）。
//
// 例如壳声明：
//   ribbon.page.file  /  ribbon.group.file.document
// 飞控包贡献一个 "姿态曲线" 按钮，填进 ribbon.group.plot.series。
//
// 业务 UI 随扩展包装卸，而骨架与模块代码永不改动。
struct UiSlot
{
    QString slotId; // 插槽标识（唯一）
    QString label;  // 展示名
    QString owner;  // 声明者（模块/骨架 id），便于归属诊断
    SlotKind kind = SlotKind::RibbonGroup;
    QString parentSlotId; // 上级插槽（ribbon group 的父页签等）；空=顶层
    int order = 0;        // 同级排序
    QString when;         // 上下文可见性（空=恒真）：Ribbon 上下文页签靠它收起/展开
};

// 已绑定到某插槽的一个动作 + 它来自哪个扩展包。
struct BoundUiAction
{
    UiAction action;
    QString provider; // pack id，用于按包撤销
};

} // namespace nextpilot::plotscope::registry
