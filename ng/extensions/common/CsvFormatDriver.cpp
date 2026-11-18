#include "CsvFormatDriver.h"

#include "DataRepository.h"
#include "Dataset.h"

#include <QFileInfo>

using namespace ng::core;

QString CsvFormatDriver::id() const
{
    return QStringLiteral("csv");
}

QString CsvFormatDriver::displayName() const
{
    return QStringLiteral("CSV 格式驱动");
}

ng::core::FormatDomain CsvFormatDriver::domain() const
{
    return ng::core::FormatDomain::General;
}

QStringList CsvFormatDriver::suffixes() const
{
    return {QStringLiteral("csv")};
}

bool CsvFormatDriver::acceptsFormat(const QString &format) const
{
    // 兼容默认：空 format 视为 csv（历史调用省略 format 时按 CSV 处理）。
    return format.isEmpty() || format == QStringLiteral("csv") || format == QStringLiteral(".csv");
}

QString CsvFormatDriver::preferredTableName(const Intent &intent, const QString &sourcePath) const
{
    // 表名策略（声明，非执行）：
    //   intent 显式指定 table -> 用它；否则用文件名（去扩展名）。
    // 与旧 driver 路径的行为保持一致。
    const QString explicitName = intent.params.value(QStringLiteral("table")).toString();
    if (!explicitName.isEmpty())
        return explicitName;
    return QFileInfo(sourcePath).completeBaseName();
}

Result<IntentResult> CsvFormatDriver::load(const Intent &intent, DataRepository &repo)
{
    Q_UNUSED(intent);
    Q_UNUSED(repo);
    // CSV 已归并到统一摄取管道（见 parserId()），宿主不会调用本函数。
    // 协议解析在 CsvParser（SPI 实现）里，只有**一份**实现。
    return fail(QStringLiteral("eunsupported"),
                QStringLiteral("CSV 已归并到统一摄取管道，请走 data.load 的管道路径"));
}
