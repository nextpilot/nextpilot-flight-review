#pragma once

#include "IFormatDriver.h"

// CSV 格式驱动：**声明式**的格式知识入口。
//
// 归并后（AGENTS.md §3 P0-1），驱动不再自己读文件、自己建 Dataset：
//   - 它声明"CSV 由哪个 SPI 解析器负责"（parserId）；
//   - 它声明"CSV 的表叫什么"（preferredTableName）；
//   - 实际的协议解析在 CsvParser（SPI 实现）里，宿主统一编排读字节与落库。
//
// 于是每种格式只有**一份**解析器实现，录制旁路只需一个 tee 就能覆盖所有格式。
class CsvFormatDriver final : public ng::core::IFormatDriver
{
public:
    QString id() const override;
    QString displayName() const override;
    ng::core::FormatDomain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    // CSV 由 SPI 解析器负责 —— 宿主据此走统一摄取管道。
    QString parserId() const override { return QStringLiteral("csv"); }

    // 表名策略：CSV 的表名取自 intent 的 table 参数，否则取文件名（与旧行为一致）。
    // 解析器只认字节、给不出表名，所以这一步留在扩展层声明。
    QString preferredTableName(const ng::core::Intent &intent,
                               const QString &sourcePath) const override;

    // 旧路径不再需要：CSV 已归并到统一摄取管道。
    // 保留空实现只为兼容 IFormatDriver 的纯虚接口；宿主不会走到这里
    // （见 DataLoadHandler：parserId 非空即走管道）。
    ng::core::Result<ng::core::IntentResult> load(
        const ng::core::Intent &intent, ng::core::DataRepository &repo) override;
};
