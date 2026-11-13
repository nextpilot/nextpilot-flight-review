#include "PsCaptureFormat.h"

#include <algorithm>
#include <cstring>

namespace ps::capture {
namespace {

constexpr char kMagic[8] = {'P', 'S', 'R', 'A', 'W', '1', '\0', '\0'};
constexpr char kIndexMagic[8] = {'P', 'S', 'R', 'I', 'D', 'X', '1', '\0'};
constexpr uint32_t kVersion = 1;
constexpr uint64_t kHeaderFixed = 40;
constexpr uint32_t kMaxString = 512u * 1024u * 1024u;

bool fail(std::string &err, const char *msg)
{
    if (err.empty())
        err = msg;
    return false;
}

void putU32(std::ofstream &f, uint32_t v)
{
    f.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void putU64(std::ofstream &f, uint64_t v)
{
    f.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void putI64(std::ofstream &f, int64_t v)
{
    f.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void putString(std::ofstream &f, const std::string &s)
{
    putU32(f, static_cast<uint32_t>(s.size()));
    if (!s.empty())
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool readU32(std::ifstream &f, uint32_t &v, std::string &err, bool &eof)
{
    eof = false;
    f.read(reinterpret_cast<char *>(&v), sizeof(v));
    if (f.gcount() == 0 && f.eof())
    {
        eof = true;
        return false;
    }
    if (f.gcount() != static_cast<std::streamsize>(sizeof(v)))
        return fail(err, "raw 记录头不完整");
    return true;
}

bool readU64(std::ifstream &f, uint64_t &v, std::string &err)
{
    f.read(reinterpret_cast<char *>(&v), sizeof(v));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(v)))
        return fail(err, "raw 文件头不完整");
    return true;
}

bool readI64(std::ifstream &f, int64_t &v, std::string &err)
{
    f.read(reinterpret_cast<char *>(&v), sizeof(v));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(v)))
        return fail(err, "raw 文件头不完整");
    return true;
}

bool readString(std::ifstream &f, std::string &s, std::string &err)
{
    uint32_t len = 0;
    bool eof = false;
    if (!readU32(f, len, err, eof))
        return fail(err, eof ? "raw 文件提前结束" : "raw 字符串长度读取失败");
    if (len > kMaxString)
        return fail(err, "raw 字符串过大");
    s.resize(len);
    if (len)
    {
        f.read(s.data(), static_cast<std::streamsize>(len));
        if (f.gcount() != static_cast<std::streamsize>(len))
            return fail(err, "raw 字符串内容不完整");
    }
    return true;
}

bool readU32Ptr(const uint8_t *&p, const uint8_t *end, uint32_t &v)
{
    if (static_cast<size_t>(end - p) < 4)
        return false;
    std::memcpy(&v, p, 4);
    p += 4;
    return true;
}

bool readU64Ptr(const uint8_t *&p, const uint8_t *end, uint64_t &v)
{
    if (static_cast<size_t>(end - p) < 8)
        return false;
    std::memcpy(&v, p, 8);
    p += 8;
    return true;
}

bool readI64Ptr(const uint8_t *&p, const uint8_t *end, int64_t &v)
{
    if (static_cast<size_t>(end - p) < 8)
        return false;
    std::memcpy(&v, p, 8);
    p += 8;
    return true;
}

bool readStringPtr(const uint8_t *&p, const uint8_t *end, std::string &s)
{
    uint32_t len = 0;
    if (!readU32Ptr(p, end, len) || static_cast<size_t>(end - p) < len)
        return false;
    s.assign(reinterpret_cast<const char *>(p), len);
    p += len;
    return true;
}

} // namespace

bool isRawFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    char magic[6] = {0};
    f.read(magic, 6);
    return f.gcount() == 6 && std::memcmp(magic, kMagic, 6) == 0;
}

RawWriter::~RawWriter()
{
    if (file_.is_open())
    {
        std::string ignored;
        close(ignored);
    }
}

bool RawWriter::open(const std::string &path, const Header &header, std::string &err)
{
    err.clear();
    if (file_.is_open())
        return fail(err, "raw writer 已打开");
    file_.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_)
        return fail(err, "无法创建 raw 文件");

    file_.write(kMagic, sizeof(kMagic));
    putU32(file_, kVersion);
    putU32(file_, 0); // header_size，字符串写完后回填
    putI64(file_, header.created_at_ns);
    putU64(file_, 0); // record_count，close 时回填
    putU64(file_, 0); // index_offset，close 时回填
    putString(file_, header.source_id);
    putString(file_, header.plugin_id);
    putString(file_, header.config_json);
    putString(file_, header.schema_blob);
    if (!file_)
        return fail(err, "写 raw 文件头失败");

    const auto pos = file_.tellp();
    if (pos < 0)
        return fail(err, "raw 文件头定位失败");
    const uint64_t headerSize = static_cast<uint64_t>(pos);
    file_.seekp(12);
    putU32(file_, static_cast<uint32_t>(headerSize));
    file_.seekp(0, std::ios::end);
    if (!file_)
        return fail(err, "回填 raw 文件头失败");

    header_ = header;
    record_count_ = 0;
    index_offset_ = 0;
    index_.clear();
    file_.flush();
    return true;
}

bool RawWriter::write(const Record &record, std::string &err)
{
    err.clear();
    if (!file_.is_open())
        return fail(err, "raw writer 未打开");

    uint32_t partCount = record.part_count ? record.part_count : 1;
    std::vector<uint32_t> parts = record.part_lengths;
    if (parts.empty())
        parts.push_back(static_cast<uint32_t>(record.payload.size()));
    if (parts.size() != partCount)
        return fail(err, "raw 分片数量与 part_lengths 不一致");
    uint64_t sum = 0;
    for (uint32_t n : parts)
        sum += n;
    if (sum != record.payload.size())
        return fail(err, "raw 分片长度和与 payload 不一致");

    const uint64_t recordSize = 8 + 4 + record.topic.size() + 4 + record.encoding.size() +
                                4 + record.schema.size() + 4 + 4ull * parts.size() +
                                record.payload.size();
    if (recordSize > 0xFFFFFFFFull)
        return fail(err, "raw 记录超过 4GiB");

    const auto pos = file_.tellp();
    if (pos < 0)
        return fail(err, "raw 记录定位失败");
    const uint64_t offset = static_cast<uint64_t>(pos);
    putU32(file_, static_cast<uint32_t>(recordSize));
    putI64(file_, record.timestamp_ns);
    putString(file_, record.topic);
    putString(file_, record.encoding);
    putString(file_, record.schema);
    putU32(file_, partCount);
    for (uint32_t n : parts)
        putU32(file_, n);
    if (!record.payload.empty())
        file_.write(reinterpret_cast<const char *>(record.payload.data()),
                    static_cast<std::streamsize>(record.payload.size()));
    if (!file_)
        return fail(err, "写 raw 记录失败");

    index_.push_back({record.timestamp_ns, offset});
    ++record_count_;
    return true;
}

bool RawWriter::close(std::string &err)
{
    err.clear();
    if (!file_.is_open())
        return true;

    const auto pos = file_.tellp();
    if (pos < 0)
        return fail(err, "raw 索引定位失败");
    index_offset_ = static_cast<uint64_t>(pos);
    file_.write(kIndexMagic, sizeof(kIndexMagic));
    putU64(file_, record_count_);
    for (const auto &e : index_)
    {
        putI64(file_, e.first);
        putU64(file_, e.second);
    }
    if (!file_)
        return fail(err, "写 raw 索引失败");

    file_.seekp(24);
    putU64(file_, record_count_);
    file_.seekp(32);
    putU64(file_, index_offset_);
    file_.seekp(0, std::ios::end);
    file_.flush();
    file_.close();
    index_.clear();
    record_count_ = 0;
    index_offset_ = 0;
    return true;
}

bool RawReader::open(const std::string &path, std::string &err)
{
    err.clear();
    file_.open(path, std::ios::binary | std::ios::in);
    if (!file_)
        return fail(err, "无法打开 raw 文件");

    char magic[8] = {0};
    file_.read(magic, sizeof(magic));
    if (file_.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
        return fail(err, "不是 PSRAW1 文件");

    uint32_t version = 0;
    bool eof = false;
    if (!readU32(file_, version, err, eof) || version != kVersion)
        return fail(err, "raw 版本不支持");

    uint32_t headerSize32 = 0;
    if (!readU32(file_, headerSize32, err, eof))
        return fail(err, eof ? "raw 文件提前结束" : "raw 文件头读取失败");
    header_size_ = headerSize32;
    if (!readI64(file_, header_.created_at_ns, err))
        return fail(err, "raw 创建时间读取失败");
    if (!readU64(file_, record_count_, err))
        return fail(err, "raw 记录数读取失败");
    if (!readU64(file_, index_offset_, err))
        return fail(err, "raw 索引偏移读取失败");
    if (!readString(file_, header_.source_id, err) ||
        !readString(file_, header_.plugin_id, err) ||
        !readString(file_, header_.config_json, err) ||
        !readString(file_, header_.schema_blob, err))
        return false;

    const auto pos = file_.tellg();
    if (pos < 0 || static_cast<uint64_t>(pos) != header_size_)
        return fail(err, "raw 文件头长度不一致");

    if (index_offset_ > 0 && !loadIndex(err))
        return false;

    pos_ = header_size_;
    return true;
}

bool RawReader::loadIndex(std::string &err)
{
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(index_offset_));
    char magic[8] = {0};
    file_.read(magic, sizeof(magic));
    if (file_.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
        std::memcmp(magic, kIndexMagic, sizeof(kIndexMagic)) != 0)
        return fail(err, "raw 索引魔数不匹配");

    uint64_t count = 0;
    if (!readU64(file_, count, err))
        return false;
    if (count > record_count_)
        return fail(err, "raw 索引条数超过记录数");

    index_.resize(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i)
    {
        int64_t ts = 0;
        uint64_t offset = 0;
        if (!readI64(file_, ts, err) || !readU64(file_, offset, err))
            return false;
        index_[i] = {ts, offset};
    }
    index_loaded_ = true;
    return true;
}

bool RawReader::next(Record &record, std::string &err)
{
    err.clear();
    if (!file_.is_open())
        return fail(err, "raw reader 未打开");
    if (index_offset_ > 0 && pos_ >= index_offset_)
        return false;

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(pos_));
    uint32_t recordSize = 0;
    bool eof = false;
    if (!readU32(file_, recordSize, err, eof))
        return false;
    if (recordSize < 8)
        return fail(err, "raw 记录长度非法");

    std::vector<uint8_t> buf(recordSize);
    file_.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(recordSize));
    if (file_.gcount() != static_cast<std::streamsize>(recordSize))
        return fail(err, "raw 记录内容不完整");

    pos_ += 4 + recordSize;

    const uint8_t *p = buf.data();
    const uint8_t *end = p + buf.size();
    if (!readI64Ptr(p, end, record.timestamp_ns) ||
        !readStringPtr(p, end, record.topic) ||
        !readStringPtr(p, end, record.encoding) ||
        !readStringPtr(p, end, record.schema) ||
        !readU32Ptr(p, end, record.part_count))
        return fail(err, "raw 记录字段不完整");
    if (record.part_count == 0 || record.part_count > 1024)
        return fail(err, "raw 分片数量非法");

    record.part_lengths.assign(record.part_count, 0);
    for (uint32_t i = 0; i < record.part_count; ++i)
    {
        if (!readU32Ptr(p, end, record.part_lengths[i]))
            return fail(err, "raw 分片长度不完整");
    }
    uint64_t sum = 0;
    for (uint32_t n : record.part_lengths)
        sum += n;
    if (sum != static_cast<uint64_t>(end - p))
        return fail(err, "raw 分片长度和与 payload 不一致");
    record.payload.assign(p, end);
    return true;
}

bool RawReader::seek(int64_t timestamp_ns, std::string &err)
{
    err.clear();
    if (!file_.is_open())
        return fail(err, "raw reader 未打开");

    if (index_loaded_ && !index_.empty())
    {
        auto it = std::lower_bound(index_.begin(), index_.end(), timestamp_ns,
                                   [](const std::pair<int64_t, uint64_t> &e, int64_t ts) {
                                       return e.first < ts;
                                   });
        pos_ = it == index_.end() ? index_offset_ : it->second;
        return true;
    }

    rewind();
    while (true)
    {
        const uint64_t offset = pos_;
        Record record;
        if (!next(record, err))
            return err.empty();
        if (record.timestamp_ns >= timestamp_ns)
        {
            pos_ = offset;
            return true;
        }
    }
}

void RawReader::rewind()
{
    pos_ = header_size_;
}

bool RawReader::atEnd() const
{
    return index_offset_ > 0 && pos_ >= index_offset_;
}

} // namespace ps::capture
