#include "Recorder.h"

#include "McapCodec.h"
#include "RawCodec.h"
#include "RecordingBackend.h"

namespace nextpilot::plotscope::rec
{

using nextpilot::plotscope::foundation::fail;

Recorder::~Recorder()
{
    close(nullptr);
}

nextpilot::plotscope::foundation::Result<std::shared_ptr<Recorder>> Recorder::create(const QString &formatId,
                                                   const QString &backendId,
                                                   const QString &endpoint,
                                                   const QByteArray &headerJson, QString *err)
{
    // 两侧独立查表：格式管"字节长什么样"，后端管"字节送到哪"。
    const nextpilot::plotscope::rec::RecordingFormat *format =
        nextpilot::plotscope::rec::RecordingFormatRegistry::format(formatId);
    if (!format)
    {
        if (err)
            *err = QStringLiteral("未注册的录制格式: %1").arg(formatId);
        return fail(QStringLiteral("eformat"), err ? *err : QString());
    }
    const nextpilot::plotscope::rec::RecordingBackend *backend =
        nextpilot::plotscope::rec::RecordingBackendRegistry::backend(backendId);
    if (!backend)
    {
        if (err)
            *err = QStringLiteral("未注册的录制后端: %1").arg(backendId);
        return fail(QStringLiteral("ebackend"), err ? *err : QString());
    }

    QString sinkErr;
    std::shared_ptr<IByteSink> sink = createByteSink(backendId, &sinkErr);
    if (!sink)
    {
        if (err)
            *err = sinkErr;
        return fail(QStringLiteral("ebackend"), sinkErr);
    }
    // 先打开输出端（建立目的地），再让成帧器往上写头。
    // 顺序不能反：成帧器的 open 会立即写 preamble。
    if (!sink->open(endpoint, &sinkErr))
    {
        if (err)
            *err = sinkErr;
        return fail(QStringLiteral("ebackend"), sinkErr);
    }

    auto rec = std::make_shared<Recorder>();
    rec->m_formatId = formatId;
    rec->m_backendId = backendId;
    rec->m_sink = sink;

    if (formatId == nextpilot::plotscope::rec::FormatId::Raw)
    {
        rec->m_raw = std::make_shared<RawRecorder>();
        if (!rec->m_raw->open(rec->m_sink, headerJson, err))
            return fail(QStringLiteral("eio"), err ? *err : QString());
    }
    else if (formatId == nextpilot::plotscope::rec::FormatId::Mcap)
    {
        rec->m_mcap = std::make_shared<McapRecorder>();
        if (!rec->m_mcap->open(rec->m_sink, QStringLiteral("plotscope"), headerJson, err))
            return fail(QStringLiteral("eio"), err ? *err : QString());
    }
    else
    {
        // 插件自定义格式：需要自己实现成帧器。
        // 当前内置成帧器只覆盖 raw / mcap，插件格式接入时在此扩展。
        if (err)
            *err = QStringLiteral("录制格式尚无内置成帧器: %1").arg(formatId);
        return fail(QStringLiteral("eformat"), err ? *err : QString());
    }

    return rec;
}

bool Recorder::writeMessage(const QString &topic, const QByteArray &payload, quint64 timeNs,
                            QString *err)
{
    if (m_mcap)
        return m_mcap->writeMessage(topic, payload, timeNs, err);
    if (m_raw)
    {
        // raw 只保真字节流：通道与时间戳不进入容器（回放仍等价于原始）。
        return m_raw->write(payload, err);
    }
    if (err)
        *err = QStringLiteral("录制器未打开");
    return false;
}

bool Recorder::writeBytes(const QByteArray &data, QString *err)
{
    if (m_raw)
        return m_raw->write(data, err);
    if (m_mcap)
    {
        // mcap 需要通道归属：无通道时用默认通道名。
        return m_mcap->writeMessage(QStringLiteral("/raw"), data, 0, err);
    }
    if (err)
        *err = QStringLiteral("录制器未打开");
    return false;
}

bool Recorder::close(QString *err)
{
    bool ok = true;
    if (m_raw)
    {
        ok = m_raw->close(err);
        m_raw.reset();
    }
    if (m_mcap)
    {
        ok = m_mcap->close(err) && ok;
        m_mcap.reset();
    }
    if (m_sink)
    {
        ok = m_sink->close(err) && ok;
        m_sink.reset();
    }
    return ok;
}

bool Recorder::isOpen() const
{
    return m_raw != nullptr || m_mcap != nullptr;
}

} // namespace nextpilot::plotscope::rec
