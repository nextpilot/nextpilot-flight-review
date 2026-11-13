#pragma once

#include "CanFrames.h"

#include <string>
#include <vector>

struct BlfLog
{
    std::vector<CanFrame> frames;
};

bool psParseBlfFile(const char *path, BlfLog &log, std::string &err);
