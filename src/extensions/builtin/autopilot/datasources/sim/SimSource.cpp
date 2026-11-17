#include "ps_extension_api.h"
#include "PsDataSourceBase.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

const char *kSchema = R"({
  "title":"实时仿真源",
  "transport":{"supported":["internal"],"default":"internal"},
  "parser":{
    "label":"解析参数",
    "fields":[
      {"key":"rate_hz","type":"int","label":"采样率","default":50,"min":1,"max":1000,"suffix":" Hz"},
      {"key":"channels","type":"int","label":"通道数","default":3,"min":1,"max":4},
      {"key":"duration_s","type":"int","label":"时长（0=不限）","default":0,"min":0,"max":3600,"suffix":" s"}
    ]
  }
})";

const char *kDefaults = R"({"transport":"internal","rate_hz":50,"channels":3,"duration_s":0})";

class SimInput final : public ps::DataSourceBase
{
public:
    uint64_t capabilities() const override
    {
        return PS_INPUT_STREAM | PS_INPUT_DIALOG;
    }

    bool loadConfig(std::string_view json, PsError *err) override
    {
        QJsonObject cfg;
        if (!json.empty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json.data(), json.size()));
            if (!doc.isObject()) {
                ps::fillError(err, 2, "config 不是 JSON 对象");
                return false;
            }
            cfg = doc.object();
        }
        rate_ = qBound(1.0, cfg.value(QStringLiteral("rate_hz")).toDouble(50.0), 1000.0);
        channels_ = qBound(1, cfg.value(QStringLiteral("channels")).toInt(3), 4);
        duration_ = qMax(0.0, cfg.value(QStringLiteral("duration_s")).toDouble(0.0));
        return true;
    }

    bool saveConfig(std::string &out, PsError *err) override
    {
        (void)err;
        QJsonObject cfg;
        cfg.insert(QStringLiteral("rate_hz"), static_cast<int>(rate_));
        cfg.insert(QStringLiteral("channels"), channels_);
        cfg.insert(QStringLiteral("duration_s"), static_cast<int>(duration_));
        out = QJsonDocument(cfg).toJson(QJsonDocument::Compact).toStdString();
        return true;
    }

    PsInputDialog dialog() const override
    {
        return PsInputDialog{nullptr, kSchema, kDefaults};
    }

    bool start(PsError *err) override
    {
        (void)err;
        generated_ = 0;
        finished_ = false;
        state_ = PS_INPUT_RUNNING;
        start_ = std::chrono::steady_clock::now();
        return true;
    }

    bool poll(PsError *err) override
    {
        if (finished_)
            return true;

        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        uint64_t upto = static_cast<uint64_t>(elapsed * rate_);
        if (upto > generated_ + 4096)
            upto = generated_ + 4096;
        if (upto <= generated_)
            return true;

        const uint32_t count = static_cast<uint32_t>(upto - generated_);
        std::vector<double> t(count);
        for (uint32_t i = 0; i < count; ++i)
            t[i] = static_cast<double>(generated_ + i) / rate_;

        std::vector<double> v(count);
        const PsSourceSink *sink = this->sink();
        if (!sink || (!sink->channel_batch && !sink->frame_batch)) {
            ps::fillError(err, 1, "宿主 sink 未绑定");
            return false;
        }

        std::vector<double> values[4];
        for (int ch = 0; ch < channels_; ++ch)
        {
            values[ch].resize(count);
            const double freq = 0.7 + 0.35 * ch;
            for (uint32_t i = 0; i < count; ++i)
            {
                const double x = t[i];
                if (ch == 0)
                    values[ch][i] = 1.2 * std::sin(2.0 * kPi * freq * x);
                else if (ch == 1)
                    values[ch][i] = 0.8 * std::sin(2.0 * kPi * freq * x + 0.7);
                else if (ch == 2)
                    values[ch][i] = 1000.0 + 180.0 * std::sin(2.0 * kPi * 0.45 * x);
                else
                    values[ch][i] = 30.0 + 2.0 * std::sin(2.0 * kPi * 0.2 * x);
            }
        }

        int32_t rc = 0;
        if (sink->frame_batch)
        {
            std::vector<int64_t> time_us(count);
            for (uint32_t i = 0; i < count; ++i)
                time_us[i] = static_cast<int64_t>(std::llround(t[i] * 1e6));
            PsFrameColumn cols[4] = {
                {"roll", "deg", PS_VALUE_F64, values[0].data(), nullptr},
                {"pitch", "deg", PS_VALUE_F64, values[1].data(), nullptr},
                {"rpm", "rpm", PS_VALUE_F64, values[2].data(), nullptr},
                {"temp", "C", PS_VALUE_F64, values[3].data(), nullptr},
            };
            PsFrameBatch batch{"sim", time_us.data(), nullptr, count,
                               static_cast<uint32_t>(channels_), cols};
            rc = sink->frame_batch(sink->ctx, &batch);
        }
        else
        {
            for (int ch = 0; ch < channels_; ++ch)
            {
                const char *name = nullptr;
                const char *unit = nullptr;
                switch (ch) {
                case 0: name = "sim.roll"; unit = "deg"; break;
                case 1: name = "sim.pitch"; unit = "deg"; break;
                case 2: name = "sim.rpm"; unit = "rpm"; break;
                default: name = "sim.temp"; unit = "C"; break;
                }
                if (sink->channel_batch(sink->ctx, name, unit, t.data(), values[ch].data(), count) != 0)
                {
                    rc = 1;
                    break;
                }
            }
        }
        if (rc != 0) {
            ps::fillError(err, 1, "宿主缓冲区已满");
            finished_ = true;
            state_ = PS_INPUT_DONE;
            return true;
        }

        generated_ = upto;
        if (duration_ > 0.0 && t[count - 1] >= duration_) {
            finished_ = true;
            state_ = PS_INPUT_DONE;
        }
        return true;
    }

    void stop() override
    {
        state_ = PS_INPUT_IDLE;
    }

    PsInputState currentState() const override { return state_; }

    bool statusJson(std::string &out) override
    {
        out = "{\"state\":\"" + std::string(state_ == PS_INPUT_RUNNING ? "running"
                                                         : state_ == PS_INPUT_DONE ? "done"
                                                                                  : "idle") +
              "\",\"samples\":" + std::to_string(generated_) + "}";
        return true;
    }

private:
    double rate_ = 50.0;
    int channels_ = 3;
    double duration_ = 0.0;
    uint64_t generated_ = 0;
    bool finished_ = false;
    PsInputState state_ = PS_INPUT_IDLE;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

} // namespace

namespace {
const PsDataSourceVtable *simVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<SimInput>(R"({"id":"data_source_sim","name":"实时仿真源","version":"1.0.0","kind":"sim","transport":"internal","family":"data_source"})");
    return &vt;
}
} // namespace


extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_sim()
{
    static const PsExtensionDescriptor desc = {
        PS_EXTENSION_ABI_VERSION,
        R"({"id":"data_source_sim","name":"实时仿真源","version":"1.0.0","kind":"sim","transport":"internal","family":"data_source"})",
        PS_FAMILY_DATA_SOURCE,
        0,
        nullptr,
        nullptr,
        nullptr,
        simVtable(),
    };
    return &desc;
}
