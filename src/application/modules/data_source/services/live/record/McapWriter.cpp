#include "record/McapWriter.h"

#include <cstring>
#include <vector>

namespace {

constexpr char kMagic[8] = {'\x89', 'M', 'C', 'A', 'P', '0', '\r', '\n'};
constexpr uint8_t kOpHeader = 0x01;
constexpr uint8_t kOpFooter = 0x02;
constexpr uint8_t kOpSchema = 0x03;
constexpr uint8_t kOpChannel = 0x04;
constexpr uint8_t kOpMessage = 0x05;
constexpr uint8_t kOpChunk = 0x06;
constexpr uint8_t kOpChunkIndex = 0x08;
constexpr uint8_t kOpSummaryOffset = 0x0E;
constexpr uint8_t kOpDataEnd = 0x0F;
constexpr uint64_t kChunkTarget = 1024ull * 1024ull;

} // namespace

McapWriter::~McapWriter()
{
    if (file_.is_open())
    {
        std::string ignored;
        close(ignored);
    }
}

void McapWriter::putU16(std::vector<uint8_t> &out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void McapWriter::putU32(std::vector<uint8_t> &out, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void McapWriter::putU64(std::vector<uint8_t> &out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void McapWriter::putString(std::vector<uint8_t> &out, const std::string &s)
{
    putU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

uint64_t McapWriter::tellp() const
{
    if (!file_) return 0;
    const auto pos = const_cast<std::ofstream &>(file_).tellp();
    return pos < 0 ? 0 : static_cast<uint64_t>(pos);
}

void McapWriter::writeRecord(uint8_t op, const std::vector<uint8_t> &body)
{
    if (!file_)
        return;
    file_.put(op);
    const uint64_t len = body.size();
    file_.write(reinterpret_cast<const char *>(&len), 8);
    if (len)
        file_.write(reinterpret_cast<const char *>(body.data()), static_cast<std::streamsize>(len));
}

bool McapWriter::open(const std::string &path, const std::string &profile, std::string &err)
{
    err.clear();
    if (file_.is_open())
    {
        err = "MCAP writer 已打开";
        return false;
    }
    file_.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_)
    {
        err = "无法创建 MCAP 文件";
        return false;
    }

    channels_.clear();
    sequences_.clear();
    channelInfo_.clear();
    chunkRecords_.clear();
    chunkIndexes_.clear();
    chunkStartTime_ = 0;
    chunkEndTime_ = 0;
    chunkHasMessage_ = false;
    next_channel_ = 1;
    failed_ = false;

    file_.write(kMagic, sizeof(kMagic));
    std::vector<uint8_t> header;
    putString(header, profile);
    putString(header, "plotscope");
    writeRecord(kOpHeader, header);

    // 标记单值 JSON：{"value": x} 的 topic 就是通道名。
    std::vector<uint8_t> schema;
    putU16(schema, schema_id_);
    putString(schema, "plotscope/value");
    putString(schema, "jsonschema");
    putString(schema, "{}");
    writeRecord(kOpSchema, schema);
    if (!file_)
    {
        err = "写 MCAP 文件头失败";
        return false;
    }
    return true;
}

uint16_t McapWriter::channelId(const std::string &topic, const std::string &encoding)
{
    const std::string key = topic + '\x1f' + encoding;
    auto it = channels_.find(key);
    if (it != channels_.end())
        return it->second;
    if (next_channel_ == 0)
        return 0;

    const uint16_t id = next_channel_++;
    channels_.emplace(key, id);
    sequences_[id] = 0;
    channelInfo_[id] = {topic, encoding};

    std::vector<uint8_t> body;
    putU16(body, id);
    putU16(body, schema_id_);
    putString(body, topic);
    putString(body, encoding);
    putU32(body, 0); // metadata
    writeRecord(kOpChannel, body);
    return id;
}

bool McapWriter::writeJson(const std::string &topic, uint64_t log_time_ns,
                           const std::string &json, std::string &err)
{
    err.clear();
    if (!file_.is_open() || failed_)
    {
        err = "MCAP writer 未打开";
        return false;
    }
    const uint16_t id = channelId(topic, "json");
    if (!id)
    {
        err = "MCAP channel 数量超限";
        return false;
    }

    appendMessageRecord(id, sequences_[id]++, log_time_ns,
                        reinterpret_cast<const uint8_t *>(json.data()),
                        static_cast<uint32_t>(json.size()));
    if (!file_)
    {
        failed_ = true;
        err = "写 MCAP message 失败";
        return false;
    }
    return true;
}

bool McapWriter::writeRaw(const std::string &topic, const std::string &encoding,
                          uint64_t log_time_ns, const uint8_t *data, uint32_t size,
                          std::string &err)
{
    err.clear();
    if (!file_.is_open() || failed_)
    {
        err = "MCAP writer 未打开";
        return false;
    }
    if (size && !data)
    {
        err = "MCAP raw payload 为空";
        return false;
    }
    const uint16_t id = channelId(topic, encoding);
    if (!id)
    {
        err = "MCAP channel 数量超限";
        return false;
    }

    appendMessageRecord(id, sequences_[id]++, log_time_ns, data, size);
    if (!file_)
    {
        failed_ = true;
        err = "写 MCAP message 失败";
        return false;
    }
    return true;
}

void McapWriter::appendMessageRecord(uint16_t channelId, uint32_t sequence,
                                     uint64_t logTime, const uint8_t *data, uint32_t size)
{
    std::vector<uint8_t> body;
    putU16(body, channelId);
    putU32(body, sequence);
    putU64(body, logTime);
    putU64(body, logTime);
    if (size && data)
        body.insert(body.end(), data, data + size);

    const uint64_t total = 1 + 8 + body.size();
    chunkRecords_.push_back(kOpMessage);
    putU64(chunkRecords_, body.size());
    chunkRecords_.insert(chunkRecords_.end(), body.begin(), body.end());
    if (!chunkHasMessage_)
    {
        chunkStartTime_ = logTime;
        chunkHasMessage_ = true;
    }
    chunkEndTime_ = logTime;
    if (chunkRecords_.size() >= kChunkTarget)
        flushChunk();
}

void McapWriter::flushChunk()
{
    if (chunkRecords_.empty() || !file_)
        return;
    const uint64_t startOffset = tellp();
    std::vector<uint8_t> body;
    putU64(body, chunkStartTime_);
    putU64(body, chunkEndTime_);
    putU64(body, chunkRecords_.size());
    putU32(body, 0);            // crc，未启用
    putString(body, "");        // compression
    putU64(body, chunkRecords_.size());
    body.insert(body.end(), chunkRecords_.begin(), chunkRecords_.end());
    const uint64_t recordsLength = chunkRecords_.size();
    writeRecord(kOpChunk, body);
    if (!file_)
    {
        failed_ = true;
        chunkRecords_.clear();
        return;
    }
    ChunkIndex idx;
    idx.startTime = chunkStartTime_;
    idx.endTime = chunkEndTime_;
    idx.startOffset = startOffset;
    idx.endOffset = tellp();
    idx.recordsLength = recordsLength;
    chunkIndexes_.push_back(idx);
    chunkRecords_.clear();
    chunkHasMessage_ = false;
}

bool McapWriter::close(std::string &err)
{
    err.clear();
    if (!file_.is_open())
        return true;

    flushChunk();
    std::vector<uint8_t> dataEnd(4, 0); // data section crc = 0，表示未启用
    writeRecord(kOpDataEnd, dataEnd);
    if (!file_)
    {
        failed_ = true;
        err = "写 MCAP DataEnd 失败";
        file_.close();
        return false;
    }

    const uint64_t summaryStart = tellp();
    std::vector<uint8_t> summarySchema;
    putU16(summarySchema, schema_id_);
    putString(summarySchema, "plotscope/value");
    putString(summarySchema, "jsonschema");
    putString(summarySchema, "{}");
    const uint64_t schemaStart = tellp();
    writeRecord(kOpSchema, summarySchema);
    const uint64_t schemaLen = tellp() - schemaStart;

    const uint64_t channelStart = tellp();
    for (const auto &it : channelInfo_)
    {
        std::vector<uint8_t> ch;
        putU16(ch, it.first);
        putU16(ch, schema_id_);
        putString(ch, it.second.first);
        putString(ch, it.second.second);
        putU32(ch, 0);
        writeRecord(kOpChannel, ch);
    }
    const uint64_t channelLen = tellp() - channelStart;

    const uint64_t indexStart = tellp();
    for (const ChunkIndex &idx : chunkIndexes_)
    {
        std::vector<uint8_t> ci;
        putU64(ci, idx.startTime);
        putU64(ci, idx.endTime);
        putU64(ci, idx.startOffset);
        putU64(ci, idx.endOffset);
        putU64(ci, 0); // message_index_offset
        putU64(ci, 0); // message_index_length
        putString(ci, "");
        putU64(ci, idx.recordsLength);
        writeRecord(kOpChunkIndex, ci);
    }
    const uint64_t indexLen = tellp() - indexStart;

    const uint64_t summaryOffsetStart = tellp();
    auto writeOffset = [this](uint8_t opcode, uint64_t start, uint64_t length) {
        std::vector<uint8_t> so;
        so.push_back(opcode);
        putU64(so, start);
        putU64(so, length);
        writeRecord(kOpSummaryOffset, so);
    };
    writeOffset(kOpSchema, schemaStart, schemaLen);
    writeOffset(kOpChannel, channelStart, channelLen);
    writeOffset(kOpChunkIndex, indexStart, indexLen);

    std::vector<uint8_t> footer;
    putU64(footer, summaryStart);
    putU64(footer, summaryOffsetStart);
    putU32(footer, 0); // summary_crc
    writeRecord(kOpFooter, footer);
    file_.write(kMagic, sizeof(kMagic));
    file_.flush();
    const bool ok = static_cast<bool>(file_);
    file_.close();
    if (!ok)
    {
        err = "写 MCAP 尾部失败";
        return false;
    }
    return true;
}
