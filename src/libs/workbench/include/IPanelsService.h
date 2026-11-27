#pragma once

// A8b-M2 裁定 52441633 §三：壳读回包声明的标题动作必须经 libs/workbench
// 服务接口，src/application/gui 里不许出现 ToolboxHub / toolbox()（§9 e) grep → 0）。
// 纯数据（Contribution.h 的 ViewTitleActionSpec）；实现放 assembly，
// 内部从唯一扩展注册表读回。壳只认接口，不认识 owner/包结构。
// （A2d 第②步：旧面板声明面 panelSpec/panels 接口随旧包面板清单退场——面板声明
// 只剩 contributes.views / contributes.customEditors 单一通道。）

#include "Contribution.h" // ViewTitleActionSpec（纯数据）

#include <QList>
#include <QString>

namespace nextpilot::plotscope::workbench {

class IPanelsService
{
public:
    virtual ~IPanelsService() = default;

    // 指定视图的标题栏动作声明（按 order 升序、同权重按 commandId 字典序）。
    virtual QList<nextpilot::plotscope::registry::ViewTitleActionSpec> titleActions(const QString &viewId) const = 0;
};

// 纯接口（无 QObject 基类）：registerSingleton 存接口地址，getService static_cast 恒安全。
} // namespace nextpilot::plotscope::workbench
