#pragma once

#include "Contribution.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 汽车知识包：预留领域，后续按 ULog 公式平移 ASC/BLF/MDF4/CAN 格式时落入本包。
class AutomotivePack final : public nextpilot::plotscope::registry::IToolPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::IToolPack> build();

    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::registry::Domain domain() const override;
    const std::vector<std::unique_ptr<nextpilot::plotscope::registry::IToolContribution>> &contributions() const override;
    std::vector<std::unique_ptr<nextpilot::plotscope::registry::IToolContribution>> &contributions() override;

private:
    nextpilot::plotscope::registry::ToolPack m_pack;
};

} // namespace nextpilot::plotscope::ext
