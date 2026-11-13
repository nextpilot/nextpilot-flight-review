#pragma once

#include "ps_plugin_api.h"
#include "ps_kernel.h"

#include <QFile>
#include <QHash>
#include <QString>
#include <QTextStream>
#include <QVector>

#include <memory>
#include <vector>

// 实时源的一路通道。PsChannelInfo 指针在对象生命周期内稳定，
// 因为底层 vector 在构造时一次性 reserve，追加不再 realloc。
struct LiveChannel
{
    QString name;
    QString unit;
    QByteArray nameBytes;
    QByteArray unitBytes;
    std::vector<double> time;
    std::vector<double> value;
    PsChannelInfo info{};

    LiveChannel(const QString &n, const QString &u, int capacity);
    void append(const double *t, const double *v, uint32_t n);
};

// 一个实时源实例的宿主侧缓冲与记录器。插件只通过 PsSourceSink 写批数据；
// 宿主决定保留多少、是否落盘、何时刷新 UI。
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
    LiveChannel *channel(const QString &name, const QString &unit);
    void appendRecord(const LiveChannel &ch, uint32_t from, uint32_t count);

    QString m_pluginId;
    QString m_label;
    QVector<LiveChannel *> m_channels;
    QHash<QString, LiveChannel *> m_byName;
    PsSourceSink m_sink{};
    QFile m_record;
    QTextStream m_out;
    QString m_recordPath;
    QString m_lastEvent;
    int m_totalSamples = 0;
    bool m_overflow = false;
    bool m_changed = false;
};
