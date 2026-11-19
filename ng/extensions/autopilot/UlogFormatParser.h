#pragma once

#include "IMessageParser.h"

// ULog 格式解析器：声明式格式知识 + 数值批输出（经 SPI UlogStreamParser 执行）。
// 身份/后缀/schema 在此声明，字节解析与落库归宿主统一摄取管道。
class UlogFormatParser final : public nextpilot::plotscope::core::IMessageParser
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::core::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("ulog"); }

    QList<nextpilot::plotscope::core::ParseOutput> outputs() const override
    {
        return {nextpilot::plotscope::core::ParseOutput::FrameBatch};
    }
};
