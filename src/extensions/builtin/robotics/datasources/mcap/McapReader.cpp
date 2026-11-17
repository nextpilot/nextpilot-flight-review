#include "McapReader.h"

#include <lz4.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>

namespace ps::mcap {
namespace {

constexpr char kMagic[8] = {'\x89', 'M', 'C', 'A', 'P', '0', '\r', '\n'};
constexpr uint8_t kOpHeader = 0x01;
constexpr uint8_t kOpFooter = 0x02;
constexpr uint8_t kOpSchema = 0x03;
constexpr uint8_t kOpChannel = 0x04;
constexpr uint8_t kOpMessage = 0x05;
constexpr uint8_t kOpChunk = 0x06;
constexpr uint8_t kOpDataEnd = 0x0F;
constexpr uint64_t kMaxRecord = 1024ull * 1024ull * 1024ull;

bool fail(std::string &err, const char *msg)
{
    if (err.empty())
        err = msg;
    return false;
}

bool readString(const uint8_t *&p, const uint8_t *end, std::string &s)
{
    if (static_cast<size_t>(end - p) < 4)
        return false;
    uint32_t len = 0;
    std::memcpy(&len, p, 4);
    p += 4;
    if (static_cast<size_t>(end - p) < len)
        return false;
    s.assign(reinterpret_cast<const char *>(p), len);
    p += len;
    return true;
}

bool readU16(const uint8_t *&p, const uint8_t *end, uint16_t &v)
{
    if (static_cast<size_t>(end - p) < 2)
        return false;
    std::memcpy(&v, p, 2);
    p += 2;
    return true;
}

bool readU32(const uint8_t *&p, const uint8_t *end, uint32_t &v)
{
    if (static_cast<size_t>(end - p) < 4)
        return false;
    std::memcpy(&v, p, 4);
    p += 4;
    return true;
}

bool readU64(const uint8_t *&p, const uint8_t *end, uint64_t &v)
{
    if (static_cast<size_t>(end - p) < 8)
        return false;
    std::memcpy(&v, p, 8);
    p += 8;
    return true;
}

} // namespace

bool Reader::open(const std::string &path, std::string &err)
{
    err.clear();
    file_.open(path, std::ios::binary | std::ios::in);
    if (!file_)
        return fail(err, "无法打开 MCAP 文件");

    char magic[8] = {0};
    file_.read(magic, sizeof(magic));
    if (file_.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
        return fail(err, "不是 MCAP 文件");

    schemas_.clear();
    channels_.clear();
    encodings_.clear();
    pending_.clear();
    pending_pos_ = 0;
    ended_ = false;
    return true;
}

bool Reader::readRecord(uint8_t &op, std::vector<uint8_t> &body, std::string &err, bool &eof)
{
    eof = false;
    file_.read(reinterpret_cast<char *>(&op), 1);
    if (file_.gcount() == 0 && file_.eof())
    {
        eof = true;
        return false;
    }
    if (file_.gcount() != 1)
        return fail(err, "MCAP 记录 opcode 不完整");

    uint64_t len = 0;
    file_.read(reinterpret_cast<char *>(&len), sizeof(len));
    if (file_.gcount() != static_cast<std::streamsize>(sizeof(len)))
        return fail(err, "MCAP 记录长度不完整");
    if (len > kMaxRecord)
        return fail(err, "MCAP 记录过大");

    body.resize(static_cast<size_t>(len));
    if (len)
    {
        file_.read(reinterpret_cast<char *>(body.data()), static_cast<std::streamsize>(len));
        if (file_.gcount() != static_cast<std::streamsize>(len))
            return fail(err, "MCAP 记录内容不完整");
    }
    return true;
}

bool Reader::parseChunk(const uint8_t *data, size_t size, std::vector<Message> &messages,
                        std::string &err)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    uint64_t start = 0, finish = 0, uncompressed = 0;
    uint32_t crc = 0;
    std::string compression;
    uint64_t recordsLen = 0;
    if (!readU64(p, end, start) || !readU64(p, end, finish) || !readU64(p, end, uncompressed) ||
        !readU32(p, end, crc) || !readString(p, end, compression) ||
        !readU64(p, end, recordsLen))
        return fail(err, "MCAP Chunk 头不完整");
    if (static_cast<size_t>(end - p) < recordsLen)
        return fail(err, "MCAP Chunk records 长度超出记录体");

    const uint8_t *records = p;
    std::vector<uint8_t> decoded;
    if (compression.empty())
    {
        decoded.assign(records, records + recordsLen);
    }
    else if (compression == "zstd")
    {
        if (uncompressed == 0 || uncompressed > kMaxRecord)
            return fail(err, "MCAP zstd chunk 解压尺寸非法");
        decoded.resize(static_cast<size_t>(uncompressed));
        const size_t n = ZSTD_decompress(decoded.data(), decoded.size(), records, recordsLen);
        if (ZSTD_isError(n))
            return fail(err, "MCAP zstd 解压失败");
        decoded.resize(n);
    }
    else if (compression == "lz4")
    {
        if (uncompressed == 0 || uncompressed > kMaxRecord)
            return fail(err, "MCAP lz4 chunk 解压尺寸非法");
        decoded.resize(static_cast<size_t>(uncompressed));
        const int n = LZ4_decompress_safe(reinterpret_cast<const char *>(records),
                                          reinterpret_cast<char *>(decoded.data()),
                                          static_cast<int>(recordsLen),
                                          static_cast<int>(decoded.size()));
        if (n < 0)
            return fail(err, "MCAP lz4 解压失败");
        decoded.resize(static_cast<size_t>(n));
    }
    else
    {
        return fail(err, ("MCAP chunk 压缩算法不支持: " + compression).c_str());
    }

    size_t off = 0;
    while (off < decoded.size())
    {
        if (decoded.size() - off < 9)
            return fail(err, "MCAP chunk 内部记录不完整");
        const uint8_t op = decoded[off];
        uint64_t len = 0;
        std::memcpy(&len, decoded.data() + off + 1, 8);
        off += 9;
        if (decoded.size() - off < len)
            return fail(err, "MCAP chunk 内部记录长度超出");
        if (!parseRecord(op, decoded.data() + off, static_cast<size_t>(len), messages, err))
            return false;
        off += static_cast<size_t>(len);
    }
    return true;
}

bool Reader::parseRecord(uint8_t op, const uint8_t *data, size_t size,
                         std::vector<Message> &messages, std::string &err)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    if (op == kOpSchema)
    {
        Schema schema;
        uint32_t dataLen = 0;
        if (!readU16(p, end, schema.id) || !readString(p, end, schema.name) ||
            !readString(p, end, schema.encoding) || !readU32(p, end, dataLen) ||
            static_cast<size_t>(end - p) < dataLen)
            return fail(err, "MCAP Schema 字段不完整");
        schema.data.assign(reinterpret_cast<const char *>(p), dataLen);
        schemas_[schema.id] = std::move(schema);
        return true;
    }
    if (op == kOpChannel)
    {
        Channel channel;
        uint32_t metadataLen = 0;
        if (!readU16(p, end, channel.id) || !readU16(p, end, channel.schema_id) ||
            !readString(p, end, channel.topic) || !readString(p, end, channel.message_encoding) ||
            !readU32(p, end, metadataLen) || static_cast<size_t>(end - p) < metadataLen)
            return fail(err, "MCAP Channel 字段不完整");
        channels_[channel.id] = std::move(channel);
        encodings_.insert(channels_[channel.id].message_encoding);
        return true;
    }
    if (op == kOpMessage)
    {
        Message message;
        uint32_t sequence = 0;
        if (!readU16(p, end, message.channel_id) || !readU32(p, end, sequence) ||
            !readU64(p, end, message.log_time) || !readU64(p, end, message.publish_time))
            return fail(err, "MCAP Message 字段不完整");
        message.sequence = sequence;
        message.data.assign(p, end);
        messages.push_back(std::move(message));
        return true;
    }
    if (op == kOpChunk)
        return parseChunk(data, size, messages, err);
    return true;
}

uint64_t Reader::bytesRead()
{
    const std::streampos pos = file_.tellg();
    return pos < 0 ? 0 : static_cast<uint64_t>(pos);
}

bool Reader::next(Message &message, std::string &err)
{
    err.clear();
    while (true)
    {
        if (pending_pos_ < pending_.size())
        {
            message = std::move(pending_[pending_pos_++]);
            if (pending_pos_ == pending_.size())
            {
                pending_.clear();
                pending_pos_ = 0;
            }
            return true;
        }
        if (ended_)
            return false;

        uint8_t op = 0;
        std::vector<uint8_t> body;
        bool eof = false;
        if (!readRecord(op, body, err, eof))
            return false;

        const uint8_t *bodyPtr = body.empty() ? reinterpret_cast<const uint8_t *>("") : body.data();
        std::vector<Message> parsed;
        if (!parseRecord(op, bodyPtr, body.size(), parsed, err))
            return false;
        for (Message &m : parsed)
            pending_.push_back(std::move(m));

        if (op == kOpDataEnd || op == kOpFooter)
            ended_ = true;
    }
}

std::vector<std::string> Reader::encodings() const
{
    return std::vector<std::string>(encodings_.begin(), encodings_.end());
}

} // namespace ps::mcap
