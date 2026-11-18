#pragma once

#include "IToolContribution.h"

#include <QString>
#include <QStringList>

namespace ng::core
{

// 脚本贡献：一个知识包提供的可复用脚本/自动化片段。
//
// 数据驱动：脚本以文本形式随包分发；由脚本器官（LuaEngine）在需要时注入执行。
// headless 下这条路径同样有效——脚本本就是 CLI/自动化的核心能力之一。
class IScriptContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Script; }

    // 脚本语言，如 "lua"；供脚本器官选择引擎。
    virtual QString language() const = 0;

    // 脚本正文（可内联，也可由实现提供一个 from-file 的懒加载版本）。
    virtual QString source() const = 0;

    // 该脚本注入后暴露的全局符号（如 "ps.ap"），便于诊断与文档生成。
    virtual QStringList exports() const { return {}; }
};

} // namespace ng::core
