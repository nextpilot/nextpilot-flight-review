#pragma once

#include "PsCaptureFormat.h"
#include "live/record/McapWriter.h"

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 实时流落盘器。只负责录制策略、队列和格式写入，不参与 transport/parser。
// 队列有界；磁盘慢时丢批并计数，不阻塞插件回调。
class StreamRecorder
{
public:
    enum class Format
    {
        Csv,
        Raw,
        Mcap,
    };

    StreamRecorder();
    ~StreamRecorder();

    StreamRecorder(const StreamRecorder &) = delete;
    StreamRecorder &operator=(const StreamRecorder &) = delete;

    static Format parseFormat(const QString &id, Format fallback = Format::Mcap);
    static QString formatId(Format format);
    static QString formatLabel(Format format);

    bool start(Format format, const QString &pluginId, const QString &label,
               const QString &configJson, QString *path, QString *err);
    void stop();

    bool recording() const { return m_running.load(); }
    QString path() const { return m_path; }
    qint64 droppedBatches() const { return m_dropped.load(); }
    QString lastError() const;

    void writeChannelBatch(const char *name, const char *unit,
                           const double *time_s, const double *value, uint32_t count);
    void writeRaw(const char *topic, const char *encoding, int64_t timestamp_ns,
                  const uint8_t *data, uint32_t size);

private:
    struct Item
    {
        enum class Kind
        {
            Channels,
            Raw,
        };

        Kind kind = Kind::Channels;
        std::string name;
        std::string unit;
        std::string topic;
        std::string encoding;
        std::vector<double> time;
        std::vector<double> value;
        std::vector<uint8_t> bytes;
        int64_t timestamp_ns = 0;

        size_t bytesUsed() const;
    };

    void run();
    bool writeItem(const Item &item);
    bool writeCsv(const Item &item);
    bool writeMcap(const Item &item);
    bool writeRawItem(const Item &item);
    bool enqueue(Item &&item);
    void closeWriter();
    void setError(const QString &error);

    Format m_format = Format::Mcap;
    QString m_pluginId;
    QString m_label;
    QString m_path;
    int64_t m_epochNs = 0;

    std::ofstream m_csv;
    ps::capture::RawWriter m_raw;
    McapWriter m_mcap;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<Item> m_queue;
    size_t m_queuedBytes = 0;
    bool m_stopping = false;
    QString m_error;

    std::atomic<bool> m_running{false};
    std::atomic<qint64> m_dropped{0};
    std::thread m_thread;
};
