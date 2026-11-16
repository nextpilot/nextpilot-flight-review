#pragma once

#include "PsLoaderCommon.h"

#include <string>
#include <utility>
#include <vector>

struct UlogLog
{
    std::vector<PsChannelData> channels;
    std::vector<std::pair<std::string, std::string>> params;
};

/* PX4/ArduPilot ULog v1：格式表 + 订阅 + 数据消息，展平规则对齐 PJ。 */
bool psParseUlog(const char *path, UlogLog &log, std::string &err);
