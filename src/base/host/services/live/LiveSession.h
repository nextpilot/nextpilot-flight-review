#pragma once

#include "ps_extension_api.h"
#include "ps_kernel.h"
#include "live/record/StreamRecorder.h"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

// 实时源实例的宿主侧缓冲与分发。本身不做文件存储，
// 录制交给独立的 StreamRecorder，便于 CSV / MCAP / RAW 统一。
class LiveSession
{
public:
    static constexpr int kChannelCapacity = 262144;

    LiveSession(QString extensionId, QString label);
    ~LiveSession();

    LiveSession(const LiveSession &) = delete;
    LiveSession &operator=(const LiveSession &) = delete;

    const QString &extensionId() const { return m_extensionId; }
    const QString &label() const { return m_label; }
    QVector<const PsChannelInfo *> channels() const;
    const PsSourceSink *sink() const { return &m_sink; }

    using RawTap = std::function<void(const char *topic, const char *encoding,
                                      int64_t timestampNs, const uint8_t *data, uint32_t size)>;
    // 共享 transport：把主插件收到的原始字节扇出给外部解析器。
    void setRawTap(RawTap tap);
    bool startRecording(StreamRecorder::Format format, const QString &configJson,
                        QString *path, QString *err);
    void stopRecording();
    bool recording() const { return m_recorder && m_recorder->recording(); }
    QString recordPath() const { return m_recordPath; }
    qint64 droppedBatches() const { return m_recorder ? m_recorder->droppedBatches() : 0; }

    int totalSamples() const { return m_totalSamples; }
    qint64 rawBytes() const { return m_rawBytes; }
    bool overflow() const { return m_overflow; }
    bool changed() const { return m_changed; }
    void clearChanged() { m_changed = false; }
    QString lastEvent() const { return m_lastEvent; }

private:
    static int32_t onChannel(void *ctx, const char *name, const char *unit,
                             const double *time_s, const double *value, uint32_t count);
    static int32_t onRaw(void *ctx, const char *topic, const char *encoding,
                         int64_t timestamp_ns, const uint8_t *data, uint32_t size);
    static int32_t onEvent(void *ctx, const char *topic, const char *payload_json);
    const PsChannelInfo *channelByName(const QString &name) const;

    QString m_extensionId;
    QString m_label;
    PsStream *m_stream = nullptr;
    PsSourceSink m_sink{};
    std::unique_ptr<StreamRecorder> m_recorder;
    RawTap m_rawTap;
    QString m_recordPath;
    QString m_lastEvent;
    int m_totalSamples = 0;
    qint64 m_rawBytes = 0;
    bool m_overflow = false;
    bool m_changed = false;
};
