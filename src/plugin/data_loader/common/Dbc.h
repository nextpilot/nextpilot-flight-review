#pragma once
#include "CanFrames.h"

#include <cstdint>
#include <string>
#include <vector>

/* 从 opts JSON 读 dbc；相对路径按日志目录解析。 */
std::string psResolveDbcOption(const char *optsJson, const char *logPath);

struct DbcSignal
{
    std::string name;
    std::string unit;
    uint32_t start = 0;
    uint32_t length = 0;
    bool littleEndian = true;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
};

struct DbcMessage
{
    uint32_t id = 0;
    bool extended = false;
    std::string name;
    uint32_t dlc = 8;
    std::vector<DbcSignal> entries;
};

class DbcDatabase
{
public:
    bool load(const std::string &path, std::string &err);
    const DbcMessage *find(uint32_t id, bool extended) const;
    bool empty() const { return messages_.empty(); }

private:
    std::vector<DbcMessage> messages_;
};

/* 按 DBC 解码，通道名 CAN<bus>.<message>.<signal>；DBC 不匹配的帧忽略。 */
std::vector<PsChannelData> psCanFramesToDbcChannels(const std::vector<CanFrame> &frames,
                                                    const DbcDatabase &dbc);
