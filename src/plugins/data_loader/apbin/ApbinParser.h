#pragma once

#include "PsLoaderCommon.h"

#include <string>
#include <utility>
#include <vector>

struct ApbinLog
{
    std::vector<PsChannelData> channels;
    std::vector<std::pair<std::string, std::string>> params;
};

/* ArduPilot DataFlash：FMT 自描述 + 定长消息。解析在插件内完成。 */
bool psParseApbin(const char *path, ApbinLog &log, std::string &err);
