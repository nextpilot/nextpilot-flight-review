#include "DataLoadHandler.h"

#include "DataRepository.h"

using namespace ng::core;

bool DataLoadHandler::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("data.load");
}

Result<IntentResult> DataLoadHandler::execute(const Intent &intent)
{
    const QString path = intent.params.value(QStringLiteral("path")).toString();
    const QString format = intent.params.value(QStringLiteral("format")).toString();

    IFormatDriver *driver = format.isEmpty()
        ? m_registry.driverForPath(path)
        : m_registry.driverForFormat(format);

    // format 显式但按后缀能兜底时再试一次（容忍 format 拼写与后缀不一致）。
    if (!driver && !format.isEmpty())
        driver = m_registry.driverForPath(path);

    if (!driver)
        return fail(QStringLiteral("eformat"),
                    QStringLiteral("没有已注册的格式驱动能处理: %1 (format=%2)")
                        .arg(path, format));

    return driver->load(intent, m_repo);
}
