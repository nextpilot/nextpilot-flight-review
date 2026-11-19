#pragma once

#include "IToolContribution.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::core
{

// 一个知识包提供的**领域面板**的描述——纯数据，不含 QWidget 指针。
//
// 为什么需要它（这是"形态随领域变"的解法）：
//   面板工厂表原本只被**视图层**（gui/views/<功能>/）使用，
//   而视图层是 app 的通用功能（数据集树、绘图画布……），形态固定。
//   但有些面板的形态**本身就是领域知识**——
//   CAN 报文浏览器长什么样，取决于 DBC 定义了哪些信号；
//   波形调参器长什么样，取决于参数表；地图轨迹面板同理。
//   这类面板：
//     · 通用视图层**不该**认识它（那会让 views 层长成领域杂物箱）；
//     · 器官也**给不出**它的形态（器官是机制，不懂业务）。
//   于是形态的归属只能是**知识包**。
//
// 分工（本接口的核心）：
//   - **描述**（本接口，纯数据，headless 可见）：知识包声明
//     "我提供一个叫 car.can-browser 的面板，标题'CAN 报文'，建议放在 dock.right"；
//   - **实现**（GUI 侧，知识包的呈现层）：注册进 PanelFactoryRegistry 的工厂。
//   两者用同一个 id 关联。
//
// 为什么描述要有 id：这是**纯数据**（core 只链接 Qt6::Core），
// 于是 headless 也能回答"这个知识包提供了哪些面板"（CLI --list-contributions panel 可查），
// 与 IUiContribution 的动机完全一致。
struct PanelSpec
{
    QString id;       // 面板类型标识（唯一），如 "car.can-browser"
    QString label;    // 显示文本，如 "CAN 报文"
    QString area;     // 建议放置的区域，如 "dock.left"（空 = 由调用方决定）
    QString icon;     // 图标名（可选）
    QString data;     // 附加数据（可选，如预置参数 JSON）
};

// 界面贡献：一个知识包向 app 骨架贡献的**领域面板**集合。
//
// 与 IUiContribution 的区别：
//   - IUiContribution 贡献的是**动作**（点一下发个意图）；
//   - IPanelContribution 贡献的是**面板**（一整块有形态的界面）。
//
// 典型用法（汽车包提供 CAN 报文面板）：
//   class AutomotivePack : public IToolPack {
//     QList<PanelSpec> panels() const override {
//         return { { "car.can-browser", "CAN 报文", "dock.right" } };
//     }
//   };
class IPanelContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Panel; }

    // 该贡献提供的领域面板（纯描述）。
    virtual QList<PanelSpec> panels() const = 0;
};

} // namespace nextpilot::plotscope::core
