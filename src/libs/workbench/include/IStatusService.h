#pragma once

// A8b-M2 裁定 52441074 §3：状态栏读回经服务接口，壳不直连 registry Hub。
// 纯数据 StatusSpec（registry::StatusSpec）与文本查询；实现放 assembly。

#include "Contribution.h" // StatusSpec（纯数据）

#include <QList>
#include <QString>

namespace nextpilot::plotscope::workbench
{

class IStatusService
{
public:
    virtual ~IStatusService() = default;

    virtual QList<nextpilot::plotscope::registry::StatusSpec> specs() const = 0;
    virtual QString text(const QString &id) const = 0;
};

} // namespace nextpilot::plotscope::workbench
