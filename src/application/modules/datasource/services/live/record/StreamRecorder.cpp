#include "record/StreamRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {

constexpr size_t kMaxQueuedBytes = 32ull * 1024ull * 1024ull;
constexpr size_t kMaxQueuedBatches = 4096;

int64_t nowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

QString safeName(QString value)
{
    for (int i = 0; i < value.size(); ++i)
    {
        const QChar c = value.at(i);
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            value[i] = QLatin1Char('_');
    }
    return value;
}


std::string jsonNumber(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}

} // namespace

size_t StreamRecorder::Item::bytesUsed() const
{
    return sizeof(Item) + name.size() + unit.size() + topic.size() + encoding.size() +
           time.size() * sizeof(double) + value.size() * sizeof(double) + bytes.size();
}

StreamRecorder::StreamRecorder() = default;

StreamRecorder::~StreamRecorder()
{
    stop();
}

StreamRecorder::Format StreamRecorder::parseFormat(const QString &id, Format fallback)
{
    const QString value = id.trimmed().toLower();
    if (value == QLatin1String("raw") || value == QLatin1String("psraw") ||
        value == QLatin1String("psraw1"))
        return Format::Raw;
    if (value == QLatin1String("mcap"))
        return Format::Mcap;
    return fallback;
}

QString StreamRecorder::formatId(Format format)
{
    switch (format)
    {
    case Format::Raw: return QStringLiteral("raw");
    case Format::Mcap: break;
    }
    return QStringLiteral("mcap");
}

QString StreamRecorder::formatLabel(Format format)
{
    switch (format)
    {
    case Format::Raw: return QStringLiteral("RAW（原始字节）");
    case Format::Mcap: break;
    }
    return QStringLiteral("MCAP（解析后）");
}

bool StreamRecorder::start(Format format, const QString &extensionId, const QString &label,
                           const QString &configJson, QString *path, QString *err)
{
    stop();

    m_format = format;
    m_extensionId = extensionId;
    m_label = label;
    m_epochNs = nowNs();

    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty())
        baseDir = QDir::tempPath();
    QString dir = QDir(baseDir).filePath(QStringLiteral("recordings"));
    if (!QDir().mkpath(dir))
    {
        // AppDataLocation 在部分只读/容器环境不可写，退回临时目录。
        dir = QDir(QDir::tempPath()).filePath(QStringLiteral("plotscope-recordings"));
        if (!QDir().mkpath(dir))
        {
            if (err)
                *err = QStringLiteral("无法创建记录目录：%1").arg(dir);
            return false;
        }
    }

    QString ext;
    switch (format)
    {
    case Format::Raw: ext = QStringLiteral("psraw"); break;
    case Format::Mcap: ext = QStringLiteral("mcap"); break;
    }
    const QString stem = QStringLiteral("%1-%2")
                             .arg(safeName(extensionId),
                                  QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyyMMdd-HHmmss-mmm")));
    m_path = QDir(dir).filePath(stem + QLatin1Char('.') + ext);

    std::string openError;
    bool opened = false;
    switch (format)
    {
    case Format::Raw:
    {
        ps::capture::Header header;
        header.source_id = extensionId.toStdString();
        header.extension_id = extensionId.toStdString();
        header.config_json = configJson.toStdString();
        header.created_at_ns = m_epochNs;
        // OEM7 的 database.json 是回放解码必需，随 raw 头一起存，避免回放另配路径。
        const QString database = QJsonDocument::fromJson(configJson.toUtf8())
                                     .object().value(QStringLiteral("database")).toString();
        if (!database.isEmpty())
        {
            QFile db(database);
            if (db.open(QIODevice::ReadOnly))
                header.schema_blob = db.readAll().toStdString();
            else
                header.schema_blob.clear();
        }
        opened = m_raw.open(m_path.toStdString(), header, openError);
        break;
    }
    case Format::Mcap:
        opened = m_mcap.open(m_path.toStdString(), "plotscope", openError);
        break;
    }

    if (!opened)
    {
        closeWriter();
        if (err)
            *err = QStringLiteral("无法创建记录文件：%1\n%2")
                       .arg(m_path, QString::fromStdString(openError));
        m_path.clear();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_queuedBytes = 0;
        m_stopping = false;
        m_error.clear();
    }
    m_dropped.store(0);
    m_running.store(true);
    m_thread = std::thread(&StreamRecorder::run, this);

    if (path)
        *path = m_path;
    return true;
}

void StreamRecorder::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_thread.joinable() && !m_running.load())
            return;
        m_stopping = true;
    }
    m_condition.notify_all();

    if (m_thread.joinable())
        m_thread.join();

    m_running.store(false);
    closeWriter();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_queuedBytes = 0;
    m_stopping = false;
}

QString StreamRecorder::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

bool StreamRecorder::enqueue(Item &&item)
{
    const size_t size = item.bytesUsed();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping || !m_running.load())
        return false;
    if (size > kMaxQueuedBytes || m_queue.size() >= kMaxQueuedBatches ||
        m_queuedBytes + size > kMaxQueuedBytes)
    {
        m_dropped.fetch_add(1);
        return false;
    }
    m_queuedBytes += size;
    m_queue.push_back(std::move(item));
    m_condition.notify_one();
    return true;
}

void StreamRecorder::writeChannelBatch(const char *name, const char *unit,
                                       const double *time_s, const double *value,
                                       uint32_t count)
{
    if (!m_running.load() || !name || !name[0] || !time_s || !value || count == 0)
        return;
    if (m_format == Format::Raw)
        return;

    Item item;
    item.kind = Item::Kind::Channels;
    item.name = name;
    item.unit = unit ? unit : "";
    item.time.assign(time_s, time_s + count);
    item.value.assign(value, value + count);
    enqueue(std::move(item));
}

void StreamRecorder::writeRaw(const char *topic, const char *encoding,
                              int64_t timestamp_ns, const uint8_t *data, uint32_t size)
{
    if (!m_running.load() || m_format != Format::Raw || !topic || !topic[0])
        return;
    if (size && !data)
        return;

    Item item;
    item.kind = Item::Kind::Raw;
    item.topic = topic;
    item.encoding = encoding ? encoding : "raw";
    item.timestamp_ns = timestamp_ns ? timestamp_ns : nowNs();
    if (size)
        item.bytes.assign(data, data + size);
    enqueue(std::move(item));
}

void StreamRecorder::run()
{
    while (true)
    {
        Item item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_stopping && m_queue.empty())
                break;
            item = std::move(m_queue.front());
            m_queue.pop_front();
            m_queuedBytes -= item.bytesUsed();
        }

        if (!writeItem(item))
        {
            m_running.store(false);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
            m_queue.clear();
            m_queuedBytes = 0;
            break;
        }
    }

    closeWriter();
}

bool StreamRecorder::writeItem(const Item &item)
{
    switch (m_format)
    {
    case Format::Raw: return writeRawItem(item);
    case Format::Mcap: return writeMcap(item);
    }
    return false;
}

bool StreamRecorder::writeMcap(const Item &item)
{
    if (!m_mcap.isOpen())
    {
        setError(QStringLiteral("MCAP 文件未打开"));
        return false;
    }
    if (item.kind == Item::Kind::Raw)
    {
        std::string err;
        if (!m_mcap.writeRaw(item.topic, item.encoding,
                             item.timestamp_ns > 0 ? static_cast<uint64_t>(item.timestamp_ns)
                                                   : static_cast<uint64_t>(nowNs()),
                             item.bytes.data(), static_cast<uint32_t>(item.bytes.size()), err))
        {
            setError(QString::fromStdString(err));
            return false;
        }
        return true;
    }

    for (size_t i = 0; i < item.time.size() && i < item.value.size(); ++i)
    {
        const int64_t ns = m_epochNs + static_cast<int64_t>(std::llround(item.time[i] * 1e9));
        const uint64_t logTime = ns > 0 ? static_cast<uint64_t>(ns) : 0;
        const std::string json = "{\"value\":" + jsonNumber(item.value[i]) + "}";
        std::string err;
        if (!m_mcap.writeJson(item.name, logTime, json, err))
        {
            setError(QString::fromStdString(err));
            return false;
        }
    }
    return true;
}

bool StreamRecorder::writeRawItem(const Item &item)
{
    if (item.kind != Item::Kind::Raw)
        return true;
    if (!m_raw.isOpen())
    {
        setError(QStringLiteral("RAW 文件未打开"));
        return false;
    }

    ps::capture::Record record;
    record.timestamp_ns = item.timestamp_ns > 0 ? item.timestamp_ns : nowNs();
    record.topic = item.topic;
    record.encoding = item.encoding;
    record.part_count = 1;
    record.part_lengths = {static_cast<uint32_t>(item.bytes.size())};
    record.payload = item.bytes;

    std::string err;
    if (!m_raw.write(record, err))
    {
        setError(QString::fromStdString(err));
        return false;
    }
    return true;
}

void StreamRecorder::closeWriter()
{
    std::string err;
    switch (m_format)
    {
    case Format::Raw:
        m_raw.close(err);
        break;
    case Format::Mcap:
        m_mcap.close(err);
        break;
    }
    if (!err.empty())
        setError(QString::fromStdString(err));
}

void StreamRecorder::setError(const QString &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_error.isEmpty())
        m_error = error;
}
