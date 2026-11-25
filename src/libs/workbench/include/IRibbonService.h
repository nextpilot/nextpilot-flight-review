#pragma once

// A8b-M2 裁定 52441074 §3：壳读回贡献数据必须经 libs/workbench 的服务接口，
// 不许在 src/application/gui 里出现 ToolboxHub / toolbox()（§9 e) grep → 0）。
// 本接口只暴露**纯数据**与"按 slot 问条目"，不暴露任何 Hub/容器类；实现放 assembly，
// 内部从唯一扩展注册表的 contributes 面读回。壳只认接口，仍不懂 owner/业务 id 分支。

#include "Contribution.h" // UiAction（纯数据）

#include <QList>
#include <QString>

namespace nextpilot::plotscope::workbench
{

class IRibbonService
{
public:
    virtual ~IRibbonService() = default;

    // 该 slot 当前静态条目（来自 commands/menus.ribbon 声明）。
    virtual QList<nextpilot::plotscope::registry::UiAction>
    actionsIn(const QString &slotId) const = 0;

    // 该 slot 的动态条目（来自 IActionProvider，运行期变化；静态条目在前）。
    // 实现方负责把 provider 的 actionsFor(slotId) 合并进来；headless 下可查。
    virtual QList<nextpilot::plotscope::registry::UiAction>
    dynamicActionsIn(const QString &slotId) const = 0;
};

} // namespace nextpilot::plotscope::workbench
