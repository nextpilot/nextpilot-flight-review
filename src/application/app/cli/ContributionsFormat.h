#pragma once

// `--contributions` 中 views 行的文本形态属 CLI 表现层，不属 registry 声明模型。
// 全仓只此一个格式化出口：CLI 打印与 e2e 断言共用，改 CLI 输出格式只动本文件。
// owner 显式传参：ToolboxHub::OwnedView 的 item.owner 未填、owner 在外层；不含行尾换行。

#include "ContributionParser.h"

namespace nextpilot::plotscope::registry {

inline QString formatContributionViewLine(const QString &owner, const ContributedView &v)
{
    return QStringLiteral("%1 views %2 container=%3 order=%4 icon=%5")
        .arg(owner, v.id, v.container, QString::number(v.order), v.icon.isEmpty() ? QStringLiteral("-") : v.icon);
}

} // namespace nextpilot::plotscope::registry
