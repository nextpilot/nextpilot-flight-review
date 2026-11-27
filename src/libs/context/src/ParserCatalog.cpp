#include "ParserCatalog.h"

#include "ParserHub.h"

#include <QFileInfo>

namespace nextpilot::plotscope::context {
namespace ParserCatalog {

namespace {
QStringList suffixesImpl()
{
    QStringList out;
    for (const QString &id : parser::ParserHub::ids())
    {
        const auto *entry = parser::ParserHub::entry(id);
        if (!entry || !entry->info.extensions)
            continue;
        for (const QString &piece :
             QString::fromUtf8(entry->info.extensions).split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            const QString ext = piece.trimmed().toLower();
            if (ext.startsWith(QLatin1Char('.')))
                out << ext;
        }
    }
    out.removeDuplicates();
    return out;
}
} // namespace

QString formatForSuffix(const QString &suffix)
{
    QString normalized = suffix.trimmed().toLower();
    if (!normalized.startsWith(QLatin1Char('.')))
        normalized.prepend(QLatin1Char('.'));
    const auto *entry = parser::ParserHub::findByExtension(normalized);
    return entry ? QString::fromUtf8(entry->info.id) : QString();
}

QString formatForPath(const QString &path)
{
    const QString suffix = QStringLiteral(".") + QFileInfo(path).suffix().toLower();
    return formatForSuffix(suffix);
}

QStringList suffixes()
{
    // 注册只在启动期完成，注册表此后只读：首次查询冻结快照。
    // 函数内静态初始化由 C++11 保证线程安全；如将来支持运行期热插拔，需改回实时查询并加锁。
    static const QStringList frozen = suffixesImpl();
    return frozen;
}

QStringList patterns()
{
    QStringList out;
    for (const QString &ext : suffixes())
        out << QStringLiteral("*") + ext;
    return out;
}

} // namespace ParserCatalog
} // namespace nextpilot::plotscope::context
