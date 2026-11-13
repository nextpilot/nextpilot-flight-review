#pragma once

#include "CanFrames.h"

#include <string>
#include <vector>

struct AscLog
{
    std::vector<CanFrame> frames;
};

bool psParseAscFile(const char *path, AscLog &log, std::string &err);
