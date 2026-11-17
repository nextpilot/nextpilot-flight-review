#pragma once

#include "ps_extension_api.h"
#include "ps_kernel.h"
#include "record/StreamRecorder.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>
#include <map>
#include <memory>
#include <vector>

// 实时数据 sink：接收 frame_batch、落 typed Dataset，并维护一份绘图 view。
// view 按表共享时间轴，不再把 time 复制到每个通道；后续 Canvas 改 typed 后这里只剩落库。
// 不是“会话”，也不持有扩展生命周期；handle/fanout 由 LiveDataSourceDriver 管。
class LiveFrameSink
{
public:
    static constexpr int kChannelCapacity = 262144;

    LiveFrameSink(QString extensionId, QString label);
    ~LiveFrameSink();

    LiveFrameSink(const LiveFrameSink &) = delete;
    LiveFrameSink &operator=(const LiveFrameSink &) = delete;

    const QString &extensionId() const { return m_extensionId; }
    const QString &label() const { return m_label; }
    QVector<const PsChannelInfo *> channels() const;
    const PsSourceSink *sink() const { return &m_sink; }
    PsDataset *dataset() const { return m_dataset; }

    using RawTap = std::function<void(const char *topic, const char *encoding,
                                      int64_t timestampNs, const uint8_t *data, uint32_t size)>;
    // 共享 transport：把主扩展收到的原始字节扇出给外部解析器。
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
    struct ChannelView
    {
        QByteArray name;
        QByteArray unit;
        QByteArray table;
        QByteArray column;
        bool typed = false;
        std::shared_ptr<std::vector<double>> time;
        std::vector<double> value; // 仅旧 channel_batch 兼容；frame_batch 不复制 f64
        PsChannelInfo info{};

        ChannelView();
        void refresh();
        void reserve();
        void trimTo(int capacity);
    };

    struct TableView
    {
        QByteArray name;
        std::shared_ptr<std::vector<double>> time;
        std::vector<std::unique_ptr<ChannelView>> columns;

        TableView();
        void reserve();
        void trimTo(int capacity);
    };

    static int32_t onChannel(void *ctx, const char *name, const char *unit,
                             const double *time_s, const double *value, uint32_t count);
    static int32_t onFrameBatch(void *ctx, const PsFrameBatch *batch);
    static int32_t onRaw(void *ctx, const char *topic, const char *encoding,
                         int64_t timestamp_ns, const uint8_t *data, uint32_t size);
    static int32_t onEvent(void *ctx, const char *topic, const char *payload_json);
    const PsChannelInfo *channelByName(const QString &name) const;
    ChannelView *legacyChannel(const QString &name, const QString &unit);
    TableView *frameTable(const PsFrameBatch *batch, int *err);

    QString m_extensionId;
    QString m_label;
    PsDataset *m_dataset = nullptr;
    std::map<QString, std::unique_ptr<TableView>> m_tables;
    std::map<QString, std::unique_ptr<ChannelView>> m_legacy;
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
