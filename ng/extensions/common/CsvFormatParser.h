#pragma once

#include "IMessageParser.h"

// CSV 格式解析器：声明式格式知识 + 数值批输出（经 SPI CsvParser 执行）。
// 身份/后缀/表名/schema/probe 在此声明，字节解析归 SPI，宿主统一编排读字节与落库。
class CsvFormatParser final : public nextpilot::plotscope::core::IMessageParser
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::core::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("csv"); }
    QString preferredTableName(const nextpilot::plotscope::core::Intent &intent,
                               const QString &sourcePath) const override;

    QList<nextpilot::plotscope::core::ParseOutput> outputs() const override
    {
        return {nextpilot::plotscope::core::ParseOutput::FrameBatch};
    }

    QJsonObject configSchema() const override;
    QJsonObject probeFile(const QString &path) const override;
    QByteArray defaultImportOptions(const QString &path) const override;
};
