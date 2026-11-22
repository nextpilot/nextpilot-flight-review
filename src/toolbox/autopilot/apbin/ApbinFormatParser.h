#pragma once

#include "IMessageParser.h"

// ArduPilot DataFlash（.bin）格式解析器：身份/后缀在这里声明，字节解析由 SPI 的 apbin 执行。
class ApbinFormatParser final : public nextpilot::plotscope::parser::IMessageParser
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::registry::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("apbin"); }

    QList<nextpilot::plotscope::parser::ParseOutput> outputs() const override
    {
        return {nextpilot::plotscope::parser::ParseOutput::FrameBatch};
    }
};
