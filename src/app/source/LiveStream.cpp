#include "source/LiveStream.h"

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

LiveChannel::LiveChannel(const QString &n, const QString &u, int capacity)
    : name(n), unit(u), nameBytes(n.toUtf8()), unitBytes(u.toUtf8())
{
    time.reserve(static_cast<size_t>(capacity));
    value.reserve(static_cast<size_t>(capacity));
    info.name = nameBytes.constData();
    info.unit = unitBytes.constData();
    info.point_count = 0;
    info.time = nullptr;
    info.value = nullptr;
}

void LiveChannel::append(const double *t, const double *v, uint32_t n)
{
    if (n == 0)
        return;
    const size_t room = time.capacity() - time.size();
    const uint32_t take = static_cast<uint32_t>(qMin<size_t>(n, room));
    for (uint32_t i = 0; i < take; ++i)
    {
        time.push_back(t[i]);
        value.push_back(v[i]);
    }
    info.point_count = static_cast<uint32_t>(time.size());
    info.time = time.data();
    info.value = value.data();
}

LiveStream::LiveStream(QString pluginId, QString label)
    : m_pluginId(std::move(pluginId)), m_label(std::move(label))
{
    m_sink.ctx = this;
    m_sink.channel_batch = &LiveStream::onChannel;
    m_sink.event = &LiveStream::onEvent;
}

LiveStream::~LiveStream()
{
    stopRecording();
    qDeleteAll(m_channels);
}

QVector<const PsChannelInfo *> LiveStream::channels() const
{
    QVector<const PsChannelInfo *> out;
    out.reserve(m_channels.size());
    for (const LiveChannel *ch : m_channels)
        out.append(&ch->info);
    return out;
}

LiveChannel *LiveStream::channel(const QString &name, const QString &unit)
{
    auto it = m_byName.find(name);
    if (it != m_byName.end())
        return it.value();
    auto *ch = new LiveChannel(name, unit, kChannelCapacity);
    m_channels.append(ch);
    m_byName.insert(name, ch);
    return ch;
}

int32_t LiveStream::onChannel(void *ctx, const char *name, const char *unit,
                              const double *time_s, const double *value, uint32_t count)
{
    auto *self = static_cast<LiveStream *>(ctx);
    if (!self || !name || !name[0] || !time_s || !value || count == 0)
        return 1;
    auto *ch = self->channel(QString::fromUtf8(name), QString::fromUtf8(unit ? unit : ""));
    const uint32_t before = static_cast<uint32_t>(ch->time.size());
    ch->append(time_s, value, count);
    const uint32_t appended = static_cast<uint32_t>(ch->time.size()) - before;
    self->m_totalSamples += static_cast<int>(appended);
    if (appended > 0)
    {
        self->m_changed = true;
        if (self->m_record.isOpen())
            self->appendRecord(*ch, before, appended);
    }
    if (appended < count)
        self->m_overflow = true;
    return appended < count ? 1 : 0;
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

void LiveStream::appendRecord(const LiveChannel &ch, uint32_t from, uint32_t count)
{
    for (uint32_t i = from; i < from + count && i < ch.time.size(); ++i)
        m_out << QString::number(ch.time[i], 'g', 10) << ',' << csvField(ch.name) << ','
              << QString::number(ch.value[i], 'g', 10) << '\n';
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
