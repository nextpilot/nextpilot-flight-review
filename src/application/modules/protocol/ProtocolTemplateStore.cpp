#include "ProtocolTemplateStore.h"

#include "protocol/ProtocolSpec.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace nextpilot::plotscope::host
{

QString ProtocolTemplateStore::suffix()
{
    return QStringLiteral(".psproto");
}

QString ProtocolTemplateStore::directory()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                      + QStringLiteral("/protocols");
    QDir().mkpath(dir);
    return dir;
}

bool ProtocolTemplateStore::validName(const QString &name, QString *err)
{
    const QString t = name.trimmed();
    if (t.isEmpty())
    {
        if (err)
            *err = QStringLiteral("模板名不能为空");
        return false;
    }
    if (t.size() > 64)
    {
        if (err)
            *err = QStringLiteral("模板名过长（≤64 字符）");
        return false;
    }
    if (t.contains(QLatin1Char('/')) || t.contains(QLatin1Char('\\')) || t.contains(QStringLiteral("..")))
    {
        if (err)
            *err = QStringLiteral("模板名不能含路径分隔符或 ..：%1").arg(name);
        return false;
    }
    for (QChar c : t)
    {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-') ||
              c == QLatin1Char('.') || c == QLatin1Char(' ')))
        {
            if (err)
                *err = QStringLiteral("模板名只能含字母/数字/下划线/中划线/点/空格：%1").arg(name);
            return false;
        }
    }
    return true;
}

QVector<QJsonObject> ProtocolTemplateStore::list(QString *err)
{
    QVector<QJsonObject> items;
    QDir dir(directory());
    if (!dir.exists())
    {
        if (err)
            *err = QStringLiteral("模板库目录不存在：%1").arg(dir.absolutePath());
        return items;
    }
    const QString suf = suffix();
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*") + suf}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
    {
        QJsonObject o;
        QString base = fi.fileName();
        if (base.endsWith(suf))
            base.chop(suf.size());
        o.insert(QStringLiteral("name"), base);
        o.insert(QStringLiteral("fileName"), fi.fileName());
        o.insert(QStringLiteral("path"), fi.absoluteFilePath());
        o.insert(QStringLiteral("sizeBytes"), static_cast<double>(fi.size()));
        o.insert(QStringLiteral("modified"), fi.lastModified().toString(Qt::ISODate));
        items.push_back(o);
    }
    return items;
}

bool ProtocolTemplateStore::read(const QString &path, QByteArray *out, QString *err, bool requireSuffix)
{
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
    {
        if (err)
            *err = QStringLiteral("模板文件不存在：%1").arg(path);
        return false;
    }
    if (requireSuffix && !fi.fileName().endsWith(suffix(), Qt::CaseInsensitive))
    {
        if (err)
            *err = QStringLiteral("不是协议模板文件（需要 %1 后缀）：%2").arg(suffix(), fi.fileName());
        return false;
    }
    if (fi.size() > kMaxTemplateBytes)
    {
        if (err)
            *err = QStringLiteral("模板文件过大（%1 字节 > 上限 %2）")
                       .arg(fi.size())
                       .arg(kMaxTemplateBytes);
        return false;
    }
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly))
    {
        if (err)
            *err = QStringLiteral("打开模板失败：%1").arg(f.errorString());
        return false;
    }
    *out = f.readAll();
    if (out->isEmpty())
    {
        if (err)
            *err = QStringLiteral("模板文件为空：%1").arg(fi.fileName());
        return false;
    }
    return true;
}

bool ProtocolTemplateStore::save(const QString &name, const QByteArray &content,
                                QString *savedPath, QString *err)
{
    QString nameErr;
    if (!validName(name, &nameErr))
    {
        if (err)
            *err = nameErr;
        return false;
    }
    // 校验与落盘都交给 saveToPath：一处实现，避免"库内写一份、编辑器写一份"。
    const QString path = QDir(directory()).absoluteFilePath(name.trimmed() + suffix());
    if (!saveToPath(path, content, err))
        return false;
    if (savedPath)
        *savedPath = path;
    return true;
}

bool ProtocolTemplateStore::saveToPath(const QString &path, const QByteArray &content, QString *err)
{
    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(content, &perr).object();
    if (perr.error != QJsonParseError::NoError)
    {
        if (err)
            *err = QStringLiteral("模板 JSON 解析失败：%1").arg(perr.errorString());
        return false;
    }
    const QStringList issues = nextpilot::plotscope::protocol::validate(root);
    if (!issues.isEmpty())
    {
        if (err)
            *err = QStringLiteral("模板校验失败：%1").arg(issues.join(QStringLiteral("；")));
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        if (err)
            *err = QStringLiteral("写入模板失败：%1").arg(f.errorString());
        return false;
    }
    // 统一用规范化缩进落盘：手工编辑的模板也保持同一份可读格式。
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit())
    {
        if (err)
            *err = QStringLiteral("提交模板失败：%1").arg(f.errorString());
        return false;
    }
    return true;
}

} // namespace nextpilot::plotscope::host
