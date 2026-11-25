#include "ExtensionScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace nextpilot::plotscope::registry
{

ExtensionScanReport scanExtensions(const QString &root)
{
    ExtensionScanReport report;
    QDir rootDir(root);
    if (!rootDir.exists())
        return report; // 空根合法（零扩展底座），不记 issue

    // 每个子目录是一个扩展候选；只认含 manifest.json 的。
    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                         QDir::Name);
    for (const QFileInfo &dirInfo : entries)
    {
        const QString manifestPath = dirInfo.absoluteFilePath() + QStringLiteral("/manifest.json");
        if (!QFile::exists(manifestPath))
            continue; // 非扩展目录，跳过
        report.scannedDirs.append(dirInfo.absoluteFilePath());

        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly))
        {
            report.issues.append(ParseIssue{dirInfo.fileName(), QStringLiteral("manifest.json 打不开")});
            continue;
        }
        const QByteArray bytes = f.readAll();
        f.close();

        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
        {
            report.issues.append(ParseIssue{dirInfo.fileName(),
                                            QStringLiteral("manifest JSON 非法: %1").arg(perr.errorString())});
            continue;
        }
        const QJsonObject manifest = doc.object();

        const QJsonValue idv = manifest.value(QStringLiteral("id"));
        if (!idv.isString() || idv.toString().isEmpty())
        {
            report.issues.append(ParseIssue{dirInfo.fileName() + QStringLiteral(".id"),
                                            QStringLiteral("manifest 缺 id，跳过")});
            continue;
        }
        const QString id = idv.toString();

        const QJsonValue cv = manifest.value(QStringLiteral("contributes"));
        if (!cv.isObject())
        {
            report.issues.append(ParseIssue{dirInfo.fileName() + QStringLiteral(".contributes"),
                                            QStringLiteral("manifest 缺 contributes 对象，跳过")});
            continue;
        }

        ParseResult pr = parseContributes(cv.toObject(), id);
        for (const auto &issue : pr.issues)
            report.issues.append(ParseIssue{dirInfo.fileName() + QLatin1Char('.') + issue.keyPath,
                                            issue.message});
        report.docs.append(pr.doc); // 即使有非致命 issue 也保留已解析部分
    }

    return report;
}

} // namespace nextpilot::plotscope::registry
