#pragma once

#include "IToolPack.h"

#include <QString>

#include <memory>

namespace ng::ext
{

// 飞控知识包：ULog 格式 + 飞控 UI/脚本/数据/例子/文档（六类齐全的样板）。
class AutopilotPack final : public ng::core::IToolPack
{
public:
    static QString packId();
    static std::unique_ptr<ng::core::IToolPack> build();

    QString id() const override;
    QString displayName() const override;
    ng::core::FormatDomain domain() const override;
    const std::vector<std::unique_ptr<ng::core::IToolContribution>> &contributions() const override;
    std::vector<std::unique_ptr<ng::core::IToolContribution>> &contributions() override;

private:
    ng::core::ToolPack m_pack;
};

} // namespace ng::ext
