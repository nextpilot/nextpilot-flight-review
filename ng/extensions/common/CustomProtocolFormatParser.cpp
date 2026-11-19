#include "CustomProtocolFormatParser.h"
#include "Intent.h"

#include "protocol/ProtocolSpec.h"
#include "protocol/SequentialCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace nextpilot::plotscope::core;

QString CustomProtocolFormatParser::id() const
{
    return QStringLiteral("custom-protocol");
}

QString CustomProtocolFormatParser::displayName() const
{
    return QStringLiteral("自定义顺序协议");
}

nextpilot::plotscope::registry::Domain CustomProtocolFormatParser::domain() const
{
    return nextpilot::plotscope::registry::Domain::General;
}

QStringList CustomProtocolFormatParser::suffixes() const
{
    // 数据文件是任意二进制，不靠后缀识别；靠 format 参数显式选择。
    return {};
}

bool CustomProtocolFormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("custom-protocol");
}

QString CustomProtocolFormatParser::preferredTableName(const Intent &intent,
                                                        const QString &sourcePath) const
{
    const QString explicitName = intent.params.value(QStringLiteral("table")).toString();
    if (!explicitName.isEmpty())
        return explicitName;
    return QFileInfo(sourcePath).completeBaseName();
}

QJsonObject CustomProtocolFormatParser::configSchema() const
{
    QJsonObject s;
    s[QStringLiteral("title")] = QStringLiteral("加载自定义协议数据");
    QJsonArray fields;
    fields.append(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("templatePath")},
        {QStringLiteral("type"), QStringLiteral("file")},
        {QStringLiteral("label"), QStringLiteral("协议模板 (.psproto)")},
        {QStringLiteral("description"), QStringLiteral("选择协议模板")},
        {QStringLiteral("flags"), QJsonObject{{QStringLiteral("fileFilter"),
                                               QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)")}}},
    });
    s[QStringLiteral("fields")] = fields;
    QJsonObject ui;
    ui[QStringLiteral("warnings")] = QJsonArray();
    s[QStringLiteral("ui")] = ui;
    return s;
}

QByteArray CustomProtocolFormatParser::prepareParserOptions(const Intent &intent,
                                                             const QByteArray &options) const
{
    QJsonObject o = QJsonDocument::fromJson(options).object();
    // 也允许 intent 参数直接给 templatePath（脚本场景，跳过对话框）。
    QString path = o.value(QStringLiteral("templatePath")).toString();
    if (path.isEmpty())
        path = intent.params.value(QStringLiteral("templatePath")).toString();
    if (!path.isEmpty())
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
        {
            const QByteArray text = f.readAll();
            const QStringList issues = nextpilot::plotscope::protocol::validate(QJsonDocument::fromJson(text).object());
            if (issues.isEmpty())
            {
                // 展开成自包含参数：解析器只需 template 文本，不接触文件系统。
                o.insert(QStringLiteral("template"), QString::fromUtf8(text));
                return QJsonDocument(o).toJson(QJsonDocument::Compact);
            }
        }
    }
    // 没有 templatePath 但 options 里已有 template（脚本直给）——原样透传。
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
