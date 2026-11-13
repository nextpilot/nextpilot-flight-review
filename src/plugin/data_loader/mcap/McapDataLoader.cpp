#include "McapReader.h"
#include "PsDataInputBase.hpp"
#include "PsLoaderCommon.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

const char *kManifest = R"({"id":"data_load_mcap","name":"MCAP 记录","version":"1.0.0","kind":"record","family":"data_input","file_extensions":[".mcap"]})";
const char *kSchema = R"({
  "title":"MCAP 记录",
  "fields":[
    {"key":"encoding","type":"string","label":"编码过滤（空=自动）","default":""}
  ]
})";
const char *kDefaults = R"({"encoding":""})";

void addSample(const QString &name, double t, double v,
               QHash<QString, int> &index, std::vector<PsChannelData> &channels)
{
    auto it = index.find(name);
    if (it == index.end())
    {
        PsChannelData c;
        c.name = name.toStdString();
        channels.push_back(std::move(c));
        it = index.insert(name, static_cast<int>(channels.size() - 1));
    }
    channels[it.value()].t.push_back(t);
    channels[it.value()].v.push_back(v);
}

void flattenJson(const QJsonValue &value, const QString &topic, const QString &path, double t,
                 QHash<QString, int> &index, std::vector<PsChannelData> &channels,
                 bool plainValue)
{
    const QString full = path.isEmpty() ? topic : (topic.isEmpty() ? path : topic + "/" + path);
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        // PlotScope StreamRecorder 写的单值消息：{"value": x}，topic 就是通道名。
        if (plainValue && path.isEmpty() && !topic.isEmpty() && obj.size() == 1 &&
            obj.contains(QStringLiteral("value")))
        {
            flattenJson(obj.value(QStringLiteral("value")), topic, QString(), t, index, channels, false);
            return;
        }
        for (auto it = obj.begin(); it != obj.end(); ++it)
            flattenJson(it.value(), topic, path.isEmpty() ? it.key() : path + "/" + it.key(),
                        t, index, channels, false);
        return;
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i)
            flattenJson(arr.at(i), topic,
                        path.isEmpty() ? QString::number(i) : path + "/" + QString::number(i),
                        t, index, channels, false);
        return;
    }
    if (value.isDouble())
    {
        if (!full.isEmpty())
            addSample(full, t, value.toDouble(), index, channels);
        return;
    }
    if (value.isBool())
    {
        if (!full.isEmpty())
            addSample(full, t, value.toBool() ? 1.0 : 0.0, index, channels);
        return;
    }
    if (value.isString())
    {
        bool ok = false;
        const double v = value.toString().toDouble(&ok);
        if (ok && !full.isEmpty())
            addSample(full, t, v, index, channels);
    }
}

class McapInput final : public ps::DataInputBase
{
public:
    uint64_t capabilities() const override { return PS_INPUT_FILE | PS_INPUT_DIALOG; }
    PsInputState currentState() const override { return PS_INPUT_IDLE; }
    bool start(PsError *err) override
    {
        (void)err;
        return true;
    }
    void stop() override {}
    bool poll(PsError *err) override
    {
        (void)err;
        return true;
    }

    bool importFile(std::string_view path, std::string_view opts, void **dataset,
                    PsError *err) override
    {
        (void)opts;
        if (!dataset || !host())
        {
            ps::fillError(err, 1, "dataset 或 host 为空");
            return false;
        }
        *dataset = nullptr;

        ps::mcap::Reader reader;
        std::string re;
        if (!reader.open(std::string(path), re))
        {
            ps::fillError(err, 2, re);
            return false;
        }

        std::vector<PsChannelData> channels;
        QHash<QString, int> index;
        std::set<std::string> unsupported;
        bool first = true;
        int64_t base = 0;
        uint64_t decoded = 0;

        ps::mcap::Message message;
        while (reader.next(message, re))
        {
            auto ch = reader.channels().find(message.channel_id);
            if (ch == reader.channels().end())
                continue;
            if (ch->second.message_encoding != "json")
            {
                unsupported.insert(ch->second.message_encoding);
                continue;
            }
            QJsonParseError pe;
            const QByteArray payload(reinterpret_cast<const char *>(message.data.data()),
                                     static_cast<int>(message.data.size()));
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &pe);
            if (pe.error != QJsonParseError::NoError)
            {
                unsupported.insert("json(无效)");
                continue;
            }
            if (first)
            {
                base = static_cast<int64_t>(message.log_time);
                first = false;
            }
            const double t = static_cast<double>(static_cast<int64_t>(message.log_time) - base) / 1e9;
            bool plainValue = false;
            if (ch->second.schema_id)
            {
                auto schema = reader.schemas().find(ch->second.schema_id);
                plainValue = schema != reader.schemas().end() && schema->second.name == "plotscope/value";
            }
            flattenJson(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
                        QString::fromStdString(ch->second.topic), QString(), t, index, channels,
                        plainValue);
            ++decoded;
        }
        if (!re.empty())
        {
            ps::fillError(err, 2, re);
            return false;
        }
        if (!unsupported.empty())
        {
            std::string names;
            for (const auto &s : unsupported)
                names += (names.empty() ? "" : ", ") + s;
            ps::fillError(err, 2, "不支持的 encoding: " + names);
            return false;
        }
        if (channels.empty())
        {
            ps::fillError(err, 2, decoded ? "JSON 中没有数值字段" : "MCAP 中没有可读消息");
            return false;
        }

        std::string buildErr;
        PsDataset *ds = psBuildDataset(channels, buildErr, host());
        if (!ds)
        {
            ps::fillError(err, 3, buildErr.empty() ? "构造数据集失败" : buildErr);
            return false;
        }
        *dataset = ds;
        return true;
    }

    PsInputDialog dialog() const override
    {
        return PsInputDialog{nullptr, kSchema, kDefaults};
    }

    bool statusJson(std::string &out) override
    {
        out = R"({"format":"mcap","encoding":"json"})";
        return true;
    }
};

const PsDataInputVtable *mcapVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<McapInput>(kManifest);
    return &vt;
}

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_load_mcap()
{
    static const PsPluginDescriptor desc = {
        PS_PLUGIN_ABI_VERSION,
        kManifest,
        PS_FAMILY_DATA_INPUT,
        0,
        nullptr,
        nullptr,
        nullptr,
        mcapVtable(),
    };
    return &desc;
}
