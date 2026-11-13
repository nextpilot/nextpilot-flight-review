#pragma once

#include "PsLoaderCommon.h"

#include <cstdint>
#include <string>
#include <vector>

struct CanFrame
{
    double t = 0;
    uint8_t bus = 1;
    uint32_t id = 0;
    bool ext = false;
    bool fd = false;
    bool rx = true;
    std::vector<uint8_t> data;
};

std::string psCanIdText(const CanFrame &f);

/* 帧 → 通道：只保留变化过的字节，并给出该 ID 的周期（ms）。 */
std::vector<PsChannelData> psCanFramesToChannels(const std::vector<CanFrame> &frames);
