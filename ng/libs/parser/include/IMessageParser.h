#pragma once

/*
 * 统一解析器接口（唯一名字，Message 取广义"数据报文"）。
 *
 * 与 SPI parser 的关系：
 *   - SPI parser（ps_parser_spi.h）：字节 → PsFrameBatch，C ABI，供 data.load 摄取管道画图；
 *   - IMessageParser：字节 → DecodedFrame（可读帧），C++ 接口，供协议检查器/打开对话框。
 *   SPI 层零改动；格式声明能力（parserId/后缀/表名/schema/probe）并入自述，
 *   一个接口同时回答"我是什么格式"和"我能解出什么"。
 *
 * 多输出：
 *   outputs() 声明本解析器能提供哪些输出。parseMessages() 产可读帧（默认委托 parse()）；
 *   parseFrames() 产数值批，交给 SPI 解析器按 parserId 执行——C++ 这层不重复实现数值解析。
 *
 * 配置纪律：解析器只从 configure 传入的 params 取值，禁止读全局（Settings/QSettings/环境/文件）。
 * prepareParserOptions 是宿主侧的编排步骤（把 templatePath 展开成内嵌文本），不属于解析器实例。
 */

#include "Intent.h"
#include "DecodedFrame.h"
#include "Domain.h"
#include "ParamSpec.h"

#include <QByteArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace nextpilot::plotscope::parser
{

using nextpilot::plotscope::registry::Domain;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::uischema::ParamSpec;

// 解析器能提供的输出类型（能力声明，调用方按需取用）。
enum class ParseOutput
{
    MsgFrame,   // 可读帧 → DecodedFrame（协议检查器）
    FrameBatch, // 数值批 → SPI PsFrameBatch（data.load 摄取管道）
};

// 输出经 Sink 交还调用者：解析器不持有结果、不决定去向。
using MsgFrameSink = std::function<bool(const DecodedFrame &)>;
using FrameBatchSink = std::function<bool(const QJsonObject &)>; // 一批数值的纯数据描述



class IMessageParser
{
public:
    virtual ~IMessageParser() = default;

    // === 身份与归属（字段语义：provider/domain 由 ExtensionRegistry::mount 写入）===
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual Domain domain() const { return Domain::General; }
    QString provider() const { return m_provider; }
    void setProvider(const QString &packId) { m_provider = packId; }

    // === 协议自述 ===
    virtual QString protocolName() const { return displayName(); }
    virtual QString usage() const { return QString(); }
    virtual QStringList supportedTransports() const { return {}; }

    // === 格式声明 ===
    // 对应的 SPI 解析器 id（如 "csv" / "ulog"）；空串表示不做数值批摄取。
    virtual QString parserId() const { return QString(); }
    virtual QStringList suffixes() const { return {}; }
    virtual bool acceptsFormat(const QString &format) const
    {
        return !format.isEmpty() && format == id();
    }
    bool acceptsSuffix(const QString &path) const
    {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix.isEmpty())
            return false;
        for (const QString &s : suffixes())
            if (suffix == s.toLower())
                return true;
        return false;
    }
    // 单表格式声明目标表名；空串表示沿用解析器给出的表名。
    virtual QString preferredTableName(const Intent &intent, const QString &sourcePath) const
    {
        Q_UNUSED(intent);
        Q_UNUSED(sourcePath);
        return QString();
    }

    // === 配置契约 ===
    // 参数规格（纯 metadata）。默认从 configSchema() 的 fields 转换，供 headless 查询与
    // uischema 渲染；字段简单的解析器可直接用，动态探测字段的解析器保留 configSchema 自渲染。
    virtual QVector<nextpilot::plotscope::uischema::ParamSpec> paramSpecs() const;
    // schema 的 JSON 视图（含 label/hint/动态列/probe 预览等），各解析器按需要覆盖。
    virtual QJsonObject configSchema() const { return QJsonObject(); }

    virtual bool configure(const QJsonObject &params, QString *err = nullptr);
    QJsonValue param(const QString &key, const QJsonValue &fallback = QJsonValue()) const;

    // === 探测与选项展开（宿主侧编排，非解析器实例行为）===
    virtual bool sniff(const QByteArray &head) const
    {
        Q_UNUSED(head);
        return false;
    }
    virtual QJsonObject probeFile(const QString &path) const
    {
        Q_UNUSED(path);
        return QJsonObject();
    }
    virtual QByteArray defaultImportOptions(const QString &path) const
    {
        Q_UNUSED(path);
        return QByteArray();
    }
    virtual QByteArray prepareParserOptions(const Intent &intent, const QByteArray &options) const
    {
        Q_UNUSED(intent);
        return options;
    }

    // === 多输出函数 ===
    virtual QList<ParseOutput> outputs() const { return {ParseOutput::MsgFrame}; }

    // 可读帧：默认委托 parse()，把单帧喂给 sink，返回喂入的帧数。
    virtual int parseMessages(const QByteArray &bytes, const MsgFrameSink &sink,
                              QString *err = nullptr) const
    {
        DecodedFrame frame;
        if (!parse(bytes, frame))
        {
            if (err)
                *err = QStringLiteral("解析失败");
            return 0;
        }
        return sink(frame) ? 1 : 0;
    }

    // 数值批：由 SPI 解析器按 parserId 执行，C++ 层不重复实现（SPI 对齐另批）。
    virtual int parseFrames(const QByteArray &bytes, const FrameBatchSink &sink,
                            QString *err = nullptr) const
    {
        Q_UNUSED(bytes);
        Q_UNUSED(sink);
        if (err)
            *err = QStringLiteral("数值批输出由 SPI 解析器提供（parserId）");
        return 0;
    }

    // 单帧解析：parseMessages 的默认实现依赖它；纯数值批解析器可不覆盖。
    virtual bool parse(const QByteArray &bytes, DecodedFrame &out) const
    {
        Q_UNUSED(bytes);
        Q_UNUSED(out);
        return false;
    }

protected:
    QJsonObject m_params; // configure 传入的参数快照；派生类只读，不写全局

private:
    QString m_provider; // 所属包 id（mount() 写入），按包原子装卸的依据
};

// === transfer 协议 id ===
namespace MessageTransport
{
inline const QString File = QStringLiteral("file");
inline const QString Memory = QStringLiteral("memory");
inline const QString Udp = QStringLiteral("udp");
inline const QString Tcp = QStringLiteral("tcp");
inline const QString Serial = QStringLiteral("serial");
inline const QString Can = QStringLiteral("can");
} // namespace MessageTransport

} // namespace nextpilot::plotscope::parser
