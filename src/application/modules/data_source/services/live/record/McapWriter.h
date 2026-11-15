#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// MCAP writer：Header + Schema/Channel + Chunk + DataEnd + Summary + Footer。
// 按 1 MiB 聚合 Chunk，summary 重复 Schema/Channel 并写 ChunkIndex。
class McapWriter
{
public:
    ~McapWriter();

    bool open(const std::string &path, const std::string &profile, std::string &err);
    bool writeJson(const std::string &topic, uint64_t log_time_ns,
                   const std::string &json, std::string &err);
    bool writeRaw(const std::string &topic, const std::string &encoding,
                  uint64_t log_time_ns, const uint8_t *data, uint32_t size,
                  std::string &err);
    bool close(std::string &err);
    bool isOpen() const { return file_.is_open(); }

private:
    uint16_t channelId(const std::string &topic, const std::string &encoding);
    void writeRecord(uint8_t op, const std::vector<uint8_t> &body);
    void putU16(std::vector<uint8_t> &out, uint16_t v);
    void putU32(std::vector<uint8_t> &out, uint32_t v);
    void putU64(std::vector<uint8_t> &out, uint64_t v);
    void putString(std::vector<uint8_t> &out, const std::string &s);
    void appendMessageRecord(uint16_t channelId, uint32_t sequence, uint64_t logTime,
                             const uint8_t *data, uint32_t size);
    void flushChunk();
    uint64_t tellp() const;
    struct ChunkIndex
    {
        uint64_t startTime = 0;
        uint64_t endTime = 0;
        uint64_t startOffset = 0;
        uint64_t endOffset = 0;
        uint64_t recordsLength = 0;
    };

    std::ofstream file_;
    std::map<std::string, uint16_t> channels_;
    std::map<uint16_t, uint32_t> sequences_;
    std::map<uint16_t, std::pair<std::string, std::string>> channelInfo_;
    std::vector<uint8_t> chunkRecords_;
    uint64_t chunkStartTime_ = 0;
    uint64_t chunkEndTime_ = 0;
    bool chunkHasMessage_ = false;
    std::vector<ChunkIndex> chunkIndexes_;
    uint16_t next_channel_ = 1;
    uint16_t schema_id_ = 1;
    bool failed_ = false;
};
