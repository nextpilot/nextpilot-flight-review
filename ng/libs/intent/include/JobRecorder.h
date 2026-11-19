#pragma once

#include "Intent.h"

#include <QVector>

namespace nextpilot::plotscope::intent
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;

// 录制业务意图序列，产出可回放的 Job。不记录 script.run 本身。
class JobRecorder
{
public:
    void begin() { m_intents.clear(); }
    void record(const Intent &intent) { m_intents.append(intent); }
    bool isRecording() const { return m_recording; }
    void setRecording(bool on) { m_recording = on; }

    Job toJob() const
    {
        Job job;
        for (const Intent &i : m_intents)
            job.append(i);
        return job;
    }

    int size() const { return m_intents.size(); }
    void clear() { m_intents.clear(); }

private:
    QVector<Intent> m_intents;
    bool m_recording = false;
};

} // namespace nextpilot::plotscope::intent
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using intent::JobRecorder;
} // namespace nextpilot::plotscope::core

