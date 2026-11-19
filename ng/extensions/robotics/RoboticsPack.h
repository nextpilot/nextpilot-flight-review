#pragma once

#include "IToolPack.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 机器人知识包：预留领域，后续平移 MCAP / ROSbag2 格式。
class RoboticsPack final : public nextpilot::plotscope::registry::IToolPack
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
