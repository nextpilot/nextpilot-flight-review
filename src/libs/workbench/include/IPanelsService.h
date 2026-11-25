#pragma once

// A8b-M2 裁定 52441633 §三：壳读回包声明的面板/标题动作必须经 libs/workbench
// 服务接口，src/application/gui 里不许出现 ToolboxHub / toolbox()（§9 e) grep → 0）。
// 纯数据（Contribution.h 的 PanelSpec / ViewTitleActionSpec）；实现放 assembly，
// 内部从唯一扩展注册表读回。壳只认接口，不认识 owner/包结构。

#include "Contribution.h" // PanelSpec / ViewTitleActionSpec（纯数据）

#include <QList>
#include <QString>

namespace nextpilot::plotscope::workbench
{

class IPanelsService
{
public:
    virtual ~IPanelsService() = default;

    // 面板声明（来自包 contributes.panels）；未命中返回 nullptr。
    virtual const nextpilot::plotscope::registry::PanelSpec *
    panelSpec(const QString &panelType) const = 0;

    // 全部面板声明（包声明全集；壳据此列"可用领域面板"）。
    virtual QList<nextpilot::plotscope::registry::PanelSpec> panels() const = 0;

    // 指定视图的标题栏动作声明（按 order 升序、同权重按 commandId 字典序）。
    virtual QList<nextpilot::plotscope::registry::ViewTitleActionSpec>
    titleActions(const QString &viewId) const = 0;
};

// 纯接口（无 QObject 基类）：registerSingleton 存接口地址，getService static_cast 恒安全。
} // namespace nextpilot::plotscope::workbench
