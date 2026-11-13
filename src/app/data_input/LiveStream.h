#pragma once

#include "ps_plugin_api.h"
#include "ps_kernel.h"

#include <QFile>
#include <QString>
#include <QTextStream>
#include <QVector>

// 实时源实例的宿主侧缓冲与记录器。缓冲本身由 kernel 的 ps-stream 持有，
// app 只负责启停、UI 刷新和落盘策略。
class LiveStream
{
public:
    static constexpr int kChannelCapacity = 262144;

    LiveStream(QString pluginId, QString label);
    ~LiveStream();

    LiveStream(const LiveStream &) = delete;
    LiveStream &operator=(const LiveStream &) = delete;

    const QString &pluginId() const { return m_pluginId; }
    const QString &label() const { return m_label; }
    QVector<const PsChannelInfo *> channels() const;
    const PsSourceSink *sink() const { return &m_sink; }

    bool startRecording(QString *path, QString *err);
    void stopRecording();
    bool recording() const { return m_record.isOpen(); }
    QString recordPath() const { return m_recordPath; }

    int totalSamples() const { return m_totalSamples; }
    bool overflow() const { return m_overflow; }
    bool changed() const { return m_changed; }
    void clearChanged() { m_changed = false; }
    QString lastEvent() const { return m_lastEvent; }

private:
    static int32_t onChannel(void *ctx, const char *name, const char *unit,
                             const double *time_s, const double *value, uint32_t count);
    static int32_t onEvent(void *ctx, const char *topic, const char *payload_json);
    const PsChannelInfo *channelByName(const QString &name) const;
    void appendRecord(const PsChannelInfo &ch, uint32_t from, uint32_t count);

    QString m_pluginId;
    QString m_label;
    PsStream *m_stream = nullptr;
    PsSourceSink m_sink{};
    QFile m_record;
    QTextStream m_out;
    QString m_recordPath;
    QString m_lastEvent;
    int m_totalSamples = 0;
    bool m_overflow = false;
    bool m_changed = false;
};
