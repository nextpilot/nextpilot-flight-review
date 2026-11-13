#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// 最小 MCAP writer：Header + Channel + Message + DataEnd + Footer。
// 不写 chunk / summary；data_load_mcap 按顺序读取即可回放。
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

    std::ofstream file_;
    std::map<std::string, uint16_t> channels_;
    std::map<uint16_t, uint32_t> sequences_;
    uint16_t next_channel_ = 1;
    uint16_t schema_id_ = 1;
    bool failed_ = false;
};
