#pragma once

#include "IFormatDriver.h"

// ULog 格式驱动：把工具箱接口接到流式 ULog 解析器上。
// 解析算法复用 UlogStreamParser（源自旧 extension 的协议知识），
// 本类只负责"身份声明 + 缓存/入库"这套所有格式驱动共用的外壳。
class UlogFormatDriver final : public nextpilot::plotscope::core::IFormatDriver
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::core::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    // 本格式由 ULog SPI 解析器负责——宿主据此走统一摄取管道
    // （读字节 -> 扇出 -> 落库），而不是让驱动自己读文件。
    // 于是 ULog 的解析实现只有**一份**（SPI 那份）。
    QString parserId() const override { return QStringLiteral("ulog"); }

    // 旧路径已退休：ULog 归并到统一摄取管道后，驱动不再自己读文件、
    // 不再管缓存与入仓（那两件事已归宿主：见 IngestOrchestrator）。
    // 这里只保留接口实现，宿主不会走到这里
    // （见 DataLoadHandler：parserId 非空即走管道）。
    nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult> load(
        const nextpilot::plotscope::core::Intent &intent, nextpilot::plotscope::core::DataRepository &repo) override;
};
