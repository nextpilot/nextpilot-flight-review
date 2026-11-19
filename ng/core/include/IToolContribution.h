#pragma once

#include "ContributionKind.h"
#include "FormatDomain.h"

#include <QString>

namespace nextpilot::plotscope::core
{

// 工具箱里任意"贡献"的最小共性。
//
// 一个知识包（IToolPack）聚合多个贡献；贡献是工具箱能登记、索引、装卸的最小单位。
// 每类贡献有各自的 typed 接口（IFormatDriver / IUiContribution / ...），
// 但它们共享这四个元数据，使注册表能统一地按 (kind, domain) 建二维索引，
// 使宿主能用一种方式装卸所有类型的能力。
//
// 注意：这里刻意不放 load()/build() 之类行为——行为属于各 typed 接口。
// 共性只回答"我是谁、我属于哪个领域、我是什么类型、我来自哪个包"。
class IToolContribution
{
public:
    virtual ~IToolContribution() = default;

    // 全局唯一标识（跨所有 kind），如 "csv" / "ulog" / "ap.ui.tuning"。
    virtual QString id() const = 0;

    virtual QString displayName() const = 0;

    // 贡献类型（开放字符串，见 ContributionKind）。
    virtual QString kind() const = 0;

    // 业务领域（稳定分类学，见 FormatDomain）。
    virtual FormatDomain domain() const = 0;

    // 来自哪个知识包（provider pack id），用于按包原子装卸。
    // 由 ToolPack 在 add() 时写入；注册表也据此把贡献归到包。
    virtual QString provider() const { return m_provider; }
    void setProvider(const QString &packId) { m_provider = packId; }

private:
    QString m_provider;
};

} // namespace nextpilot::plotscope::core
