#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ps::mcap {

struct Schema
{
    uint16_t id = 0;
    std::string name;
    std::string encoding;
    std::string data;
};

struct Channel
{
    uint16_t id = 0;
    uint16_t schema_id = 0;
    std::string topic;
    std::string message_encoding;
};

struct Message
{
    uint16_t channel_id = 0;
    uint32_t sequence = 0;
    uint64_t log_time = 0;
    uint64_t publish_time = 0;
    std::vector<uint8_t> data;
};

class Reader
{
public:
    bool open(const std::string &path, std::string &err);
    bool next(Message &message, std::string &err);
    uint64_t bytesRead();

    const std::map<uint16_t, Schema> &schemas() const { return schemas_; }
    const std::map<uint16_t, Channel> &channels() const { return channels_; }
    std::vector<std::string> encodings() const;

private:
    bool readRecord(uint8_t &op, std::vector<uint8_t> &body, std::string &err, bool &eof);
    bool parseRecord(uint8_t op, const uint8_t *data, size_t size,
                     std::vector<Message> &messages, std::string &err);
    bool parseChunk(const uint8_t *data, size_t size, std::vector<Message> &messages,
                    std::string &err);

    std::ifstream file_;
    std::map<uint16_t, Schema> schemas_;
    std::map<uint16_t, Channel> channels_;
    std::set<std::string> encodings_;
    std::vector<Message> pending_;
    size_t pending_pos_ = 0;
    bool ended_ = false;
};

} // namespace ps::mcap
