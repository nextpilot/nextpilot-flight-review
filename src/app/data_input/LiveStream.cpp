#include "data_input/LiveStream.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <cmath>

namespace {

QString csvField(QString s)
{
    s.replace(QLatin1Char(','), QLatin1Char('_'));
    s.replace(QLatin1Char('\n'), QLatin1Char('_'));
    return s;
}

} // namespace

LiveStream::LiveStream(QString pluginId, QString label)
    : m_pluginId(std::move(pluginId)), m_label(std::move(label)),
      m_stream(ps_stream_new(static_cast<uint32_t>(kChannelCapacity)))
{
    m_sink.ctx = this;
    m_sink.channel_batch = &LiveStream::onChannel;
    m_sink.event = &LiveStream::onEvent;
}

LiveStream::~LiveStream()
{
    stopRecording();
    ps_stream_free(m_stream);
}

QVector<const PsChannelInfo *> LiveStream::channels() const
{
    QVector<const PsChannelInfo *> out;
    const uint32_t n = ps_stream_channel_count(m_stream);
    out.reserve(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_stream_channel(m_stream, i))
            out.append(ch);
    return out;
}

const PsChannelInfo *LiveStream::channelByName(const QString &name) const
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

int32_t LiveStream::onChannel(void *ctx, const char *name, const char *unit,
                              const double *time_s, const double *value, uint32_t count)
{
    auto *self = static_cast<LiveStream *>(ctx);
    if (!self || !name || !name[0] || !time_s || !value || count == 0)
        return 1;
    const QString channelName = QString::fromUtf8(name);
    const PsChannelInfo *beforeInfo = self->channelByName(channelName);
    const uint32_t before = beforeInfo ? beforeInfo->point_count : 0;
    const int rc = ps_stream_push(self->m_stream, name, unit ? unit : "", time_s, value, count);
    const PsChannelInfo *afterInfo = self->channelByName(channelName);
    const uint32_t after = afterInfo ? afterInfo->point_count : before;
    const uint32_t appended = after - before;
    self->m_totalSamples += static_cast<int>(appended);
    if (appended > 0)
    {
        self->m_changed = true;
        if (self->m_record.isOpen())
            self->appendRecord(*afterInfo, before, appended);
    }
    if (rc != 0 || appended < count)
        self->m_overflow = true;
    return rc;
}

int32_t LiveStream::onEvent(void *ctx, const char *topic, const char *payload_json)
{
    auto *self = static_cast<LiveStream *>(ctx);
    if (!self || !topic)
        return 1;
    self->m_lastEvent = QStringLiteral("%1 %2").arg(QString::fromUtf8(topic),
                                                    QString::fromUtf8(payload_json ? payload_json : ""));
    self->m_changed = true;
    return 0;
}

void LiveStream::appendRecord(const PsChannelInfo &ch, uint32_t from, uint32_t count)
{
    for (uint32_t i = from; i < from + count && i < ch.point_count; ++i)
        m_out << QString::number(ch.time[i], 'g', 10) << ',' << csvField(QString::fromUtf8(ch.name))
              << ',' << QString::number(ch.value[i], 'g', 10) << '\n';
}

bool LiveStream::startRecording(QString *path, QString *err)
{
    stopRecording();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                        QStringLiteral("/recordings");
    if (!QDir().mkpath(dir))
    {
        if (err)
            *err = QStringLiteral("无法创建记录目录：%1").arg(dir);
        return false;
    }
    const QString base = QStringLiteral("%1-%2.csv")
                             .arg(csvField(m_pluginId),
                                  QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    m_recordPath = QDir(dir).filePath(base);
    m_record.setFileName(m_recordPath);
    if (!m_record.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (err)
            *err = QStringLiteral("无法创建记录文件：%1").arg(m_recordPath);
        m_recordPath.clear();
        return false;
    }
    m_out.setDevice(&m_record);
    m_out << "time,channel,value\n";
    if (path)
        *path = m_recordPath;
    return true;
}

void LiveStream::stopRecording()
{
    if (!m_record.isOpen())
        return;
    m_out.flush();
    m_record.close();
}
