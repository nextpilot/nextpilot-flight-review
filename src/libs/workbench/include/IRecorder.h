#pragma once

// A1b §1.2：意图录制接缝。按 JobRecorder 现用法裁剪。签名只出现 libs 类型。

#include "Intent.h"

namespace nextpilot::plotscope::recording
{

using nextpilot::plotscope::intent::Intent;

class IRecorder
{
public:
    virtual ~IRecorder() = default;

    virtual void begin() = 0;
    virtual void record(const Intent &intent) = 0;
    virtual bool isRecording() const = 0;
    virtual void setRecording(bool on) = 0;
    virtual int size() const = 0;
    virtual void clear() = 0;
};

} // namespace nextpilot::plotscope::recording
