#pragma once

#include "PsLoaderCommon.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct UlogMessage
{
    double t = 0.0;
    uint8_t level = 0;
    uint16_t tag = 0;
    bool tagged = false;
    std::string text;
};

struct UlogLog
{
    std::vector<PsChannelData> channels;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<std::pair<std::string, std::string>> info;
    std::vector<std::pair<double, std::pair<std::string, std::string>>> changedParams;
    std::vector<UlogMessage> messages;
    std::vector<std::pair<double, uint16_t>> dropouts;
};

/* PX4/ArduPilot ULog v1：格式表 + 订阅 + 数据消息，展平规则对齐 PJ。 */
bool psParseUlog(const char *path, UlogLog &log, std::string &err);
