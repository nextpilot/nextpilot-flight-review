#include "LiveFrameSink.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace
{
constexpr int kReserveRows = LiveFrameSink::kChannelCapacity + 65536;

bool appendTypedColumn(const PsFrameColumn &col, uint32_t rows, std::vector<double> &out)
{
    out.resize(rows);
    const uint8_t *nulls = col.nulls;
    auto isNull = [nulls](uint32_t row) {
        return nulls && (nulls[row >> 3] & (1u << (row & 7))) != 0;
    };
    for (uint32_t r = 0; r < rows; ++r)
    {
        if (isNull(r))
        {
            out[r] = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        switch (col.type)
        {
        case PS_VALUE_F64: out[r] = static_cast<const double *>(col.values)[r]; break;
        case PS_VALUE_F32: out[r] = static_cast<const float *>(col.values)[r]; break;
        case PS_VALUE_I64: out[r] = static_cast<double>(static_cast<const int64_t *>(col.values)[r]); break;
        case PS_VALUE_U64: out[r] = static_cast<double>(static_cast<const uint64_t *>(col.values)[r]); break;
        case PS_VALUE_I32: out[r] = static_cast<double>(static_cast<const int32_t *>(col.values)[r]); break;
        case PS_VALUE_U32: out[r] = static_cast<double>(static_cast<const uint32_t *>(col.values)[r]); break;
        case PS_VALUE_I16: out[r] = static_cast<double>(static_cast<const int16_t *>(col.values)[r]); break;
        case PS_VALUE_U16: out[r] = static_cast<double>(static_cast<const uint16_t *>(col.values)[r]); break;
        case PS_VALUE_I8: out[r] = static_cast<double>(static_cast<const int8_t *>(col.values)[r]); break;
        case PS_VALUE_U8: out[r] = static_cast<double>(static_cast<const uint8_t *>(col.values)[r]); break;
        case PS_VALUE_BOOL: out[r] = static_cast<const uint8_t *>(col.values)[r] ? 1.0 : 0.0; break;
        default: return false; // Text 暂不进实时绘图 view
        }
    }
    return true;
}
} // namespace

LiveFrameSink::ChannelView::ChannelView()
    : time(std::make_shared<std::vector<double>>())
{
    reserve();
}

void LiveFrameSink::ChannelView::reserve()
{
    if (time && time->capacity() < static_cast<size_t>(kReserveRows))
        time->reserve(kReserveRows);
    value.reserve(kReserveRows);
}

void LiveFrameSink::ChannelView::refresh()
{
    info.name = name.constData();
    info.unit = unit.constData();
    info.point_count = static_cast<uint32_t>(time ? time->size() : 0);
    info.time = time ? time->data() : nullptr;
    info.value = value.data();
}

void LiveFrameSink::ChannelView::trimTo(int capacity)
{
    if (!time || static_cast<int>(time->size()) <= capacity)
        return;
    const size_t drop = time->size() - static_cast<size_t>(capacity);
    time->erase(time->begin(), time->begin() + static_cast<long>(drop));
    if (value.size() > drop)
        value.erase(value.begin(), value.begin() + static_cast<long>(drop));
    refresh();
}

LiveFrameSink::TableView::TableView()
    : time(std::make_shared<std::vector<double>>())
{
    time->reserve(kReserveRows);
}

void LiveFrameSink::TableView::reserve()
{
    if (time->capacity() < static_cast<size_t>(kReserveRows))
        time->reserve(kReserveRows);
    for (auto &c : columns)
        c->reserve();
}

void LiveFrameSink::TableView::trimTo(int capacity)
{
    if (static_cast<int>(time->size()) <= capacity)
        return;
    const size_t drop = time->size() - static_cast<size_t>(capacity);
    time->erase(time->begin(), time->begin() + static_cast<long>(drop));
    for (auto &c : columns)
    {
        if (c->value.size() > drop)
            c->value.erase(c->value.begin(), c->value.begin() + static_cast<long>(drop));
        c->refresh();
    }
}

LiveFrameSink::LiveFrameSink(QString extensionId, QString label)
    : m_extensionId(std::move(extensionId)), m_label(std::move(label)),
      m_dataset(ps_dataset_new())
{
    if (m_dataset)
    {
        char err[256] = {};
        ps_dataset_set_ring_budget(m_dataset, nullptr, 32ull * 1024 * 1024, err, sizeof(err));
    }
    m_sink.ctx = this;
    m_sink.channel_batch = &LiveFrameSink::onChannel;
    m_sink.frame_batch = &LiveFrameSink::onFrameBatch;
    m_sink.event = &LiveFrameSink::onEvent;
}

LiveFrameSink::~LiveFrameSink()
{
    stopRecording();
    if (m_dataset)
        ps_dataset_free(m_dataset);
}

QVector<const PsChannelInfo *> LiveFrameSink::channels() const
{
    QVector<const PsChannelInfo *> out;
    for (const auto &it : m_tables)
        for (const auto &c : it.second->columns)
            out.append(&c->info);
    for (const auto &it : m_legacy)
        out.append(&it.second->info);
    return out;
}

const PsChannelInfo *LiveFrameSink::channelByName(const QString &name) const
{
    for (const auto &it : m_tables)
        for (const auto &c : it.second->columns)
            if (QString::fromUtf8(c->name) == name)
                return &c->info;
    const auto it = m_legacy.find(name);
    return it == m_legacy.end() ? nullptr : &it->second->info;
}

LiveFrameSink::ChannelView *LiveFrameSink::legacyChannel(const QString &name, const QString &unit)
{
    auto it = m_legacy.find(name);
    if (it != m_legacy.end())
        return it->second.get();
    auto view = std::make_unique<ChannelView>();
    view->name = name.toUtf8();
    view->unit = unit.toUtf8();
    view->refresh();
    auto *raw = view.get();
    m_legacy[name] = std::move(view);
    return raw;
}

LiveFrameSink::TableView *LiveFrameSink::frameTable(const PsFrameBatch *batch, int *err)
{
    const QString key = QString::fromUtf8(batch->table);
    auto it = m_tables.find(key);
    if (it != m_tables.end())
        return it->second.get();

    auto table = std::make_unique<TableView>();
    table->name = QByteArray(batch->table);
    for (uint32_t i = 0; i < batch->columns; ++i)
    {
        const PsFrameColumn &col = batch->column_data[i];
        if (!col.name || !col.name[0] || !col.values)
        {
            if (err) *err = 2;
            return nullptr;
        }
        auto view = std::make_unique<ChannelView>();
        view->time = table->time;
        view->name = table->name + '.' + QByteArray(col.name);
        view->unit = QByteArray(col.unit ? col.unit : "");
        view->refresh();
        table->columns.push_back(std::move(view));
    }
    table->reserve();
    auto *raw = table.get();
    m_tables[key] = std::move(table);
    return raw;
}

int32_t LiveFrameSink::onChannel(void *ctx, const char *name, const char *unit,
                                 const double *time_s, const double *value, uint32_t count)
{
    auto *self = static_cast<LiveFrameSink *>(ctx);
    if (!self || !name || !name[0] || !time_s || !value || count == 0)
        return 1;

    if (self->m_recorder)
        self->m_recorder->writeChannelBatch(name, unit ? unit : "", time_s, value, count);

    auto *view = self->legacyChannel(QString::fromUtf8(name), QString::fromUtf8(unit ? unit : ""));
    view->time->insert(view->time->end(), time_s, time_s + count);
    view->value.insert(view->value.end(), value, value + count);
    if (static_cast<int>(view->time->size()) > kChannelCapacity)
        self->m_overflow = true;
    view->trimTo(kChannelCapacity);
    self->m_totalSamples += static_cast<int>(count);
    self->m_changed = true;
    return 0;
}

int32_t LiveFrameSink::onFrameBatch(void *ctx, const PsFrameBatch *batch)
{
    auto *self = static_cast<LiveFrameSink *>(ctx);
    if (!self || !batch || !batch->table || !batch->table[0] ||
        !batch->time_us || !batch->column_data ||
        batch->rows == 0 || batch->columns == 0)
        return 1;

    if (self->m_dataset)
    {
        char err[256] = {};
        if (ps_dataset_append_frame(self->m_dataset, batch->table, batch->time_us, batch->rows,
                                    batch->column_data, batch->columns, err, sizeof(err)) != 0)
            return 4;
    }

    int err = 0;
    auto *table = self->frameTable(batch, &err);
    if (!table)
        return err ? err : 2;
    if (table->columns.size() != batch->columns)
        return 2;
    for (uint32_t i = 0; i < batch->columns; ++i)
    {
        const PsFrameColumn &col = batch->column_data[i];
        const QByteArray fullName = table->name + '.' + QByteArray(col.name ? col.name : "");
        if (!col.name || table->columns[i]->name != fullName ||
            table->columns[i]->unit != QByteArray(col.unit ? col.unit : ""))
            return 2;
    }

    std::vector<double> rowValues(batch->rows);
    std::vector<double> timeSec(batch->rows);
    for (uint32_t r = 0; r < batch->rows; ++r)
        timeSec[r] = static_cast<double>(batch->time_us[r]) * 1e-6;

    // 时间轴只追加一次，所有列共享。
    table->time->insert(table->time->end(), timeSec.begin(), timeSec.end());
    for (uint32_t i = 0; i < batch->columns; ++i)
    {
        const PsFrameColumn &col = batch->column_data[i];
        if (!appendTypedColumn(col, batch->rows, rowValues))
            return 3;
        table->columns[i]->value.insert(table->columns[i]->value.end(),
                                        rowValues.begin(), rowValues.end());
        if (self->m_recorder)
        {
            const QByteArray full = table->name + '.' + QByteArray(col.name);
            self->m_recorder->writeChannelBatch(full.constData(),
                                                table->columns[i]->unit.constData(),
                                                timeSec.data(), rowValues.data(), batch->rows);
        }
    }

    if (static_cast<int>(table->time->size()) > kChannelCapacity)
        self->m_overflow = true;
    table->trimTo(kChannelCapacity);
    for (auto &c : table->columns)
        c->refresh();
    self->m_totalSamples += static_cast<int>(batch->rows) * static_cast<int>(batch->columns);
    self->m_changed = true;
    return 0;
}

int32_t LiveFrameSink::onRaw(void *ctx, const char *topic, const char *encoding,
                             int64_t timestamp_ns, const uint8_t *data, uint32_t size)
{
    auto *self = static_cast<LiveFrameSink *>(ctx);
    if (!self || !topic)
        return 1;
    self->m_rawBytes += static_cast<qint64>(size);
    if (self->m_rawTap)
        self->m_rawTap(topic, encoding ? encoding : "raw", timestamp_ns, data, size);
    if (self->m_recorder)
        self->m_recorder->writeRaw(topic, encoding ? encoding : "raw", timestamp_ns, data, size);
    return 0;
}

int32_t LiveFrameSink::onEvent(void *ctx, const char *topic, const char *payload_json)
{
    auto *self = static_cast<LiveFrameSink *>(ctx);
    if (!self || !topic)
        return 1;
    self->m_lastEvent = QStringLiteral("%1 %2").arg(QString::fromUtf8(topic),
                                                    QString::fromUtf8(payload_json ? payload_json : ""));
    self->m_changed = true;
    return 0;
}

void LiveFrameSink::setRawTap(RawTap tap)
{
    m_rawTap = std::move(tap);
    m_sink.raw_batch = m_rawTap || (m_recorder && m_recorder->recording())
                           ? &LiveFrameSink::onRaw
                           : nullptr;
}

bool LiveFrameSink::startRecording(StreamRecorder::Format format, const QString &configJson,
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
        m_sink.raw_batch = &LiveFrameSink::onRaw;
    if (path)
        *path = m_recordPath;
    return true;
}

void LiveFrameSink::stopRecording()
{
    m_sink.raw_batch = m_rawTap ? &LiveFrameSink::onRaw : nullptr;
    if (m_recorder)
    {
        m_recorder->stop();
        m_recorder.reset();
    }
    m_recordPath.clear();
}
