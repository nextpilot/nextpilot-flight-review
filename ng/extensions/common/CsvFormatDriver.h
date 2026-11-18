#pragma once

#include "IFormatDriver.h"

// CSV 格式驱动：把工具箱接口接到流式 CSV 解析器上。
// 解析算法（逐行 + 固定分块 appendFrame）源自旧 CsvExtension，
// 本类只负责身份声明 + 缓存/入库这套共用外壳。
class CsvFormatDriver final : public ng::core::IFormatDriver
{
public:
    QString id() const override;
    QString displayName() const override;
    ng::core::FormatDomain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;
    ng::core::Result<ng::core::IntentResult> load(
        const ng::core::Intent &intent, ng::core::DataRepository &repo) override;
};
