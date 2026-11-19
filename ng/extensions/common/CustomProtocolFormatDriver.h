#pragma once

#include "IFormatDriver.h"

// 自定义顺序协议驱动：把"按 .psproto 模板解析二进制帧"接成一条正式数据格式。
//
// 它没有固定后缀（数据文件是任意二进制），靠 format=custom-protocol 显式选择；
// 配置来自通用导入对话框的 file 字段（templatePath）。
// 驱动把 templatePath 文件读成模板文本注入 parser 的 configure——
// 读配置文件是格式知识的职责，解析器自身仍不读任何文件（铁律）。
class CustomProtocolFormatDriver final : public nextpilot::plotscope::core::IFormatDriver
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::core::FormatDomain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("custom-protocol"); }
    QString preferredTableName(const nextpilot::plotscope::core::Intent &intent,
                               const QString &sourcePath) const override;

    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> load(
        const nextpilot::plotscope::core::Intent &intent, nextpilot::plotscope::core::DataRepository &repo) override;

    QJsonObject importSchema() const override;
    QByteArray prepareParserOptions(const nextpilot::plotscope::core::Intent &intent,
                                    const QByteArray &options) const override;
};
