#pragma once

#include "IMessageParser.h"

#include <QHash>
#include <QVector>

namespace nextpilot::plotscope::ext
{

// NMEA 0183 协议解析器——"协议解析器如何写"的样板。
//
// 它演示 IMessageParser 的全部要点：
//   - 自述：协议名 / 使用范围（领域+用途）/ 支持 transfer / 配置 schema；
//   - configure：只从传入参数取配置（database_path 协议模板路径），不读全局；
//   - parse：字节 → DecodedFrame（可读字段树），不认识 Dataset、不认识 GUI。
//
// 与老代码 NmeaMessageParser 的关键差别（这正是"升级"点）：
//   老版在构造函数里从 QCoreApplication::applicationDirPath()/当前目录等**全局路径**
//   扫模板文件——无法单测、无法重放、两个实例会互相串味。
//   新版默认用内置精简模板；要换模板只能由宿主经 configure 传 database_path。
class NmeaMessageParser final : public nextpilot::plotscope::core::IMessageParser
{
public:
    struct FieldDef
    {
        QString name;
        QString type; // string/number/int/time/dm
        QString unit;
        QString label;
        int index = -1;
        int hemiIndex = -1;
    };

    NmeaMessageParser();

    // === 身份（IToolContribution）===
    QString id() const override { return QStringLiteral("nmea"); }
    QString displayName() const override { return QStringLiteral("NMEA 协议解析器"); }
    nextpilot::plotscope::core::Domain domain() const override { return nextpilot::plotscope::core::Domain::Gnss; }

    // === 自述 ===
    QString protocolName() const override { return QStringLiteral("NMEA 0183"); }
    QString usage() const override
    {
        return QStringLiteral("解析 GNSS 接收机的 NMEA 0183 语句（GGA/RMC/GSV 等），"
                              "校验和校验 + 字段解码 + 度分转十进制度，供协议检查器逐帧查看。");
    }
    QStringList supportedTransports() const override
    {
        return {nextpilot::plotscope::core::MessageTransport::File, nextpilot::plotscope::core::MessageTransport::Serial,
                nextpilot::plotscope::core::MessageTransport::Udp, nextpilot::plotscope::core::MessageTransport::Memory};
    }
    QJsonObject configSchema() const override;

    // === 生命周期 ===
    bool configure(const QJsonObject &params, QString *err = nullptr) override;

    // === 行为 ===
    bool sniff(const QByteArray &head) const override;
    bool parse(const QByteArray &bytes, nextpilot::plotscope::core::DecodedFrame &out) const override;

    // 供单测：当前生效的句子模板数量。
    int templateCount() const { return m_sentences.size(); }

private:
    static QHash<QString, QVector<FieldDef>> parseTemplate(const QByteArray &json);
    static QByteArray builtinTemplate();

    QHash<QString, QVector<FieldDef>> m_sentences; // configure 填充；默认内置模板
};

} // namespace nextpilot::plotscope::ext
