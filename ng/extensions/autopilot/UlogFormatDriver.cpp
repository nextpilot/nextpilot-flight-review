#include "UlogFormatDriver.h"

#include "DataRepository.h"
using namespace ng::core;

QString UlogFormatDriver::id() const
{
    return QStringLiteral("ulog");
}

QString UlogFormatDriver::displayName() const
{
    return QStringLiteral("ULog 格式驱动");
}

ng::core::FormatDomain UlogFormatDriver::domain() const
{
    return ng::core::FormatDomain::Autopilot;
}

QStringList UlogFormatDriver::suffixes() const
{
    return {QStringLiteral("ulg"), QStringLiteral("ulog")};
}

bool UlogFormatDriver::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("ulog") || format == QStringLiteral(".ulg");
}

// 旧路径已退休。
//
// ULog 归并到统一摄取管道（AGENTS.md §3 P0-1）之后：
//   - 读字节   -> 宿主（IngestOrchestrator + IByteSource）
//   - 缓存     -> 宿主（按源文件指纹，见 IngestOrchestrator）
//   - stable id-> 宿主（见 IngestOptions）
//   - 协议解析 -> SPI 解析器（UlogStreamParser，与旧路径**共用同一份**实现）
//
// 驱动退回纯声明角色：只说"ULog 由 ulog 解析器负责"。
Result<IntentResult> UlogFormatDriver::load(const Intent &intent, DataRepository &repo)
{
    Q_UNUSED(intent);
    Q_UNUSED(repo);
    return fail(QStringLiteral("eunsupported"),
                QStringLiteral("ULog 已归并到统一摄取管道，请走 data.load 的管道路径"));
}
