#pragma once

// D12：headless 用 NullContributionSink 校验贡献。
//
// 它是一个"什么都不呈现"的接收器：接收扩展包的六类贡献数据，校验合法性
// （必填字段、重复 id、非法 when 键空间），返回错误清单，不创建任何 QWidget。
// GUI 用真实控件消费贡献；headless/CLI 用本 sink 只做校验——两条路共享同一份
// 贡献数据，差异只在"呈现 vs 校验"，因此 headless 也能发现坏贡献。

#include "Contribution.h"

#include <QSet>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

class NullContributionSink
{
public:
    // 校验一个包的全部贡献；返回错误清单（空 = 全部合法）。
    static QStringList validate(const Pack &pack)
    {
        QStringList errors;
        if (pack.id.isEmpty())
            errors << QStringLiteral("包 id 为空");

        // 每类贡献的必填 id；重复 id 报错（与 Registry 先到者为准一致，但坏数据要暴露）。
        QSet<QString> seen;
        auto checkId = [&errors, &seen](const QString &id, const char *kind) {
            if (id.isEmpty())
            {
                errors << QStringLiteral("%1 存在空 id").arg(QLatin1String(kind));
                return;
            }
            const QString key = QStringLiteral("%1:%2").arg(QLatin1String(kind), id);
            if (seen.contains(key))
                errors << QStringLiteral("%1 id 重复: %2").arg(QLatin1String(kind), id);
            seen.insert(key);
        };

        for (const auto &a : pack.uiActions) checkId(a.id, "uiAction");
        for (const auto &p : pack.panels) checkId(p.id, "panel");
        for (const auto &s : pack.scripts) checkId(s.id, "script");
        for (const auto &d : pack.dataAssets) checkId(d.id, "dataAsset");
        for (const auto &d : pack.docs) checkId(d.id, "doc");
        for (const auto &e : pack.examples) checkId(e.id, "example");
        for (const auto &r : pack.reports) checkId(r.id, "report");
        for (const auto &t : pack.themeTokens) checkId(t.id, "themeToken");

        // 视图标题/条目上下文：必须指明归属视图与命令（否则壳无法呈现）。
        for (const auto &v : pack.viewTitleActions)
            if (v.viewId.isEmpty() || v.commandId.isEmpty())
                errors << QStringLiteral("viewTitleAction 缺 viewId/commandId");
        for (const auto &i : pack.itemContexts)
            if (i.viewId.isEmpty() || i.commandId.isEmpty())
                errors << QStringLiteral("itemContext 缺 viewId/commandId");

        return errors;
    }

    // 接收即校验；返回是否干净。headless 装配期调用，坏贡献早暴露。
    static bool accept(const Pack &pack, QStringList *errors = nullptr)
    {
        const QStringList e = validate(pack);
        if (errors)
            *errors = e;
        return e.isEmpty();
    }
};

} // namespace nextpilot::plotscope::registry
