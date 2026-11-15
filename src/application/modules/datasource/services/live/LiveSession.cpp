#include "LiveSession.h"

#include <utility>

LiveSession::LiveSession(QString extensionId, QString label)
    : m_extensionId(std::move(extensionId)), m_label(std::move(label)),
      m_stream(ps_stream_new(static_cast<uint32_t>(kChannelCapacity)))
{
    m_sink.ctx = this;
    m_sink.channel_batch = &LiveSession::onChannel;
    m_sink.event = &LiveSession::onEvent;
}

LiveSession::~LiveSession()
{
    stopRecording();
    ps_stream_free(m_stream);
}

QVector<const PsChannelInfo *> LiveSession::channels() const
{
    QVector<const PsChannelInfo *> out;
    const uint32_t n = ps_stream_channel_count(m_stream);
    out.reserve(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_stream_channel(m_stream, i))
            out.append(ch);
    return out;
}

const PsChannelInfo *LiveSession::channelByName(const QString &name) const
{
    const uint32_t n = ps_stream_channel_count(m_stream);
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_stream_channel(m_stream, i);
        if (ch && QString::fromUtf8(ch->name) == name)
            return ch;
    }
    return nullptr;
}

int32_t LiveSession::onChannel(void *ctx, const char *name, const char *unit,
                              const double *time_s, const double *value, uint32_t count)
{
    auto *self = static_cast<LiveSession *>(ctx);
    if (!self || !name || !name[0] || !time_s || !value || count == 0)
        return 1;

    // 录制直接收原始批次，不依赖 ring 里是否还保留这段数据。
    if (self->m_recorder)
        self->m_recorder->writeChannelBatch(name, unit ? unit : "", time_s, value, count);

    const QString channelName = QString::fromUtf8(name);
    const PsChannelInfo *beforeInfo = self->channelByName(channelName);
    const uint32_t before = beforeInfo ? beforeInfo->point_count : 0;
    const int rc = ps_stream_push(self->m_stream, name, unit ? unit : "", time_s, value, count);
    const PsChannelInfo *afterInfo = self->channelByName(channelName);
    const uint32_t after = afterInfo ? afterInfo->point_count : before;
    const uint32_t appended = after - before;
    self->m_totalSamples += static_cast<int>(appended);
    if (appended > 0)
        self->m_changed = true;
    if (rc != 0 || appended < count)
        self->m_overflow = true;
    return rc;
}

int32_t LiveSession::onRaw(void *ctx, const char *topic, const char *encoding,
                           int64_t timestamp_ns, const uint8_t *data, uint32_t size)
{
    auto *self = static_cast<LiveSession *>(ctx);
    if (!self || !topic)
        return 1;
    self->m_rawBytes += static_cast<qint64>(size);
    if (self->m_rawTap)
        self->m_rawTap(topic, encoding ? encoding : "raw", timestamp_ns, data, size);
    if (self->m_recorder)
        self->m_recorder->writeRaw(topic, encoding ? encoding : "raw", timestamp_ns, data, size);
    return 0;
}

int32_t LiveSession::onEvent(void *ctx, const char *topic, const char *payload_json)
{
    auto *self = static_cast<LiveSession *>(ctx);
    if (!self || !topic)
        return 1;
    self->m_lastEvent = QStringLiteral("%1 %2").arg(QString::fromUtf8(topic),
                                                    QString::fromUtf8(payload_json ? payload_json : ""));
    self->m_changed = true;
    return 0;
}

void LiveSession::setRawTap(RawTap tap)
{
    m_rawTap = std::move(tap);
    m_sink.raw_batch = m_rawTap || (m_recorder && m_recorder->recording())
                           ? &LiveSession::onRaw
                           : nullptr;
}

bool LiveSession::startRecording(StreamRecorder::Format format, const QString &configJson,
                                 QString *path, QString *err)
{
    stopRecording();
    auto recorder = std::make_unique<StreamRecorder>();
    QString recordPath;
    if (!recorder->start(format, m_extensionId, m_label, configJson, &recordPath, err))
        return false;
    m_recorder = std::move(recorder);
    m_recordPath = recordPath;
    if (format == StreamRecorder::Format::Raw || m_rawTap)
        m_sink.raw_batch = &LiveSession::onRaw;
    if (path)
        *path = m_recordPath;
    return true;
}

void LiveSession::stopRecording()
{
    m_sink.raw_batch = m_rawTap ? &LiveSession::onRaw : nullptr;
    if (m_recorder)
    {
        m_recorder->stop();
        m_recorder.reset();
    }
    m_recordPath.clear();
}
