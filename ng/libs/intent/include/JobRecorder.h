#pragma once

#include "Intent.h"

#include <QVector>

namespace nextpilot::plotscope::core
{

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

} // namespace nextpilot::plotscope::core
