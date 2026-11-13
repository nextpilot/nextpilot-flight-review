#include "live/record/McapWriter.h"

#include <cstring>
#include <vector>

namespace {

constexpr char kMagic[8] = {'\x89', 'M', 'C', 'A', 'P', '0', '\r', '\n'};
constexpr uint8_t kOpHeader = 0x01;
constexpr uint8_t kOpFooter = 0x02;
constexpr uint8_t kOpSchema = 0x03;
constexpr uint8_t kOpChannel = 0x04;
constexpr uint8_t kOpMessage = 0x05;
constexpr uint8_t kOpDataEnd = 0x0F;

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

    std::vector<uint8_t> body;
    putU16(body, id);
    putU32(body, sequences_[id]++);
    putU64(body, log_time_ns);
    putU64(body, log_time_ns);
    body.insert(body.end(), json.begin(), json.end());
    writeRecord(kOpMessage, body);
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

    std::vector<uint8_t> body;
    putU16(body, id);
    putU32(body, sequences_[id]++);
    putU64(body, log_time_ns);
    putU64(body, log_time_ns);
    if (size)
        body.insert(body.end(), data, data + size);
    writeRecord(kOpMessage, body);
    if (!file_)
    {
        failed_ = true;
        err = "写 MCAP message 失败";
        return false;
    }
    return true;
}

bool McapWriter::close(std::string &err)
{
    err.clear();
    if (!file_.is_open())
        return true;

    std::vector<uint8_t> dataEnd(4, 0); // data section crc = 0，表示未启用
    writeRecord(kOpDataEnd, dataEnd);

    std::vector<uint8_t> footer;
    putU64(footer, 0); // summary_start，无 summary
    putU64(footer, 0); // summary_offset_start
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
