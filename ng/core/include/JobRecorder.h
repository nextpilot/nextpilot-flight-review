#pragma once

#include "Intent.h"

#include <QVector>

namespace nextpilot::plotscope::core
{

// 意图录制器：把执行过的意图序列录制成可回放的 Job。
// 录制只记录"门面提交的业务意图"（如 data.load / plot.render），
// 不记录 script.run 本身——回放时得到的是纯业务意图序列，干净可版本管理。
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
