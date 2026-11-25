#pragma once

// A1b §1.2：意图回放接缝。按 Job 现用法裁剪（load/size/at/clear）。

#include "Intent.h"
#include "Result.h"

#include <QJsonObject>

namespace nextpilot::plotscope::recording
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::intent::Intent;

class IPlayback
{
public:
    virtual ~IPlayback() = default;

    virtual bool load(const QJsonObject &jobJson, Result<void> *outError = nullptr) = 0;
    virtual bool isLoaded() const = 0;
    virtual int size() const = 0;
    virtual const Intent *at(int i) const = 0;
    virtual void clear() = 0;
};

} // namespace nextpilot::plotscope::recording
