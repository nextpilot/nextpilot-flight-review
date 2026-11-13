#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace ps::capture {

/* 一个逻辑消息：时间戳、来源 topic、编码、可选 schema、分片边界和原始字节。
 * raw 与 MCAP 都使用这套字段，区别只在容器。 */
struct Record
{
    int64_t timestamp_ns = 0;
    std::string topic;
    std::string encoding;
    std::string schema;
    uint32_t part_count = 1;
    std::vector<uint32_t> part_lengths;
    std::vector<uint8_t> payload;
};

struct Header
{
    std::string source_id;
    std::string plugin_id;
    std::string config_json = "{}";
    std::string schema_blob;
    int64_t created_at_ns = 0;
};

/* 只检查前 6 字节，用于区分 PSRAW1 和旧裸字节文件。 */
bool isRawFile(const std::string &path);

class RawWriter
{
public:
    ~RawWriter();

    bool open(const std::string &path, const Header &header, std::string &err);
    bool write(const Record &record, std::string &err);
    bool close(std::string &err);
    bool isOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
    Header header_;
    uint64_t record_count_ = 0;
    uint64_t index_offset_ = 0;
    std::vector<std::pair<int64_t, uint64_t>> index_;
};

class RawReader
{
public:
    bool open(const std::string &path, std::string &err);
    bool next(Record &record, std::string &err);
    bool seek(int64_t timestamp_ns, std::string &err);
    void rewind();
    bool atEnd() const;
    const Header &header() const { return header_; }
    uint64_t recordCount() const { return record_count_; }

private:
    bool loadIndex(std::string &err);

    std::ifstream file_;
    Header header_;
    uint64_t header_size_ = 0;
    uint64_t record_count_ = 0;
    uint64_t index_offset_ = 0;
    uint64_t pos_ = 0;
    bool index_loaded_ = false;
    std::vector<std::pair<int64_t, uint64_t>> index_;
};

} // namespace ps::capture
