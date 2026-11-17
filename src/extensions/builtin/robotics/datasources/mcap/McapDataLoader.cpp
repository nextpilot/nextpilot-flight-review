#include "McapReader.h"
#include "PsDataSourceBase.hpp"
#include "PsLoaderCommon.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFileInfo>
#include <QString>

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

const char *kManifest = R"({"id":"data_load_mcap","name":"MCAP 记录","version":"1.0.0","kind":"record","family":"data_source","file_extensions":[".mcap"]})";
const char *kSchema = R"({
  "title":"MCAP 记录",
  "fields":[
    {"key":"encoding","type":"string","label":"编码过滤（空=自动）","default":""}
  ]
})";
const char *kDefaults = R"({"encoding":""})";

struct Table
{
    std::vector<double> t;
    std::map<std::string, std::vector<double>> cols;
};

void flattenJson(const QJsonValue &value, const QString &path, QMap<QString, double> &fields)
{
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            flattenJson(it.value(), path.isEmpty() ? it.key() : path + "/" + it.key(), fields);
        return;
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i)
            flattenJson(arr.at(i), path.isEmpty() ? QString::number(i)
                                                  : path + "/" + QString::number(i),
                        fields);
        return;
    }
    if (path.isEmpty())
        return;
    if (value.isDouble())
        fields.insert(path, value.toDouble());
    else if (value.isBool())
        fields.insert(path, value.toBool() ? 1.0 : 0.0);
    else if (value.isString())
    {
        bool ok = false;
        const double v = value.toString().toDouble(&ok);
        if (ok)
            fields.insert(path, v);
    }
}

void appendMessage(std::map<std::string, Table> &tables, const QString &topic,
                   const QMap<QString, double> &fields, double t)
{
    if (topic.isEmpty() || fields.isEmpty())
        return;
    Table &table = tables[topic.toStdString()];
    table.t.push_back(t);
    const size_t row = table.t.size();
    for (auto it = fields.begin(); it != fields.end(); ++it)
    {
        auto &col = table.cols[it.key().toStdString()];
        while (col.size() + 1 < row)
            col.push_back(std::numeric_limits<double>::quiet_NaN());
        col.push_back(it.value());
    }
}

class McapInput final : public ps::DataSourceBase
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
        psLoaderSetHost(host());

        ps::mcap::Reader reader;
        std::string re;
        if (!reader.open(std::string(path), re))
        {
            ps::fillError(err, 2, re);
            return false;
        }

        const uint64_t fileSize = static_cast<uint64_t>(QFileInfo(QString::fromStdString(std::string(path))).size());
        std::map<std::string, Table> tables;
        std::set<std::string> unsupported;
        bool first = true;
        int64_t base = 0;
        uint64_t decoded = 0;

        ps::mcap::Message message;
        while (reader.next(message, re))
        {
            if (fileSize)
                psLoaderProgress(std::min<uint64_t>(95, 10 + reader.bytesRead() * 85 / fileSize), 100);
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
            QMap<QString, double> fields;
            flattenJson(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
                        QString(), fields);
            if (plainValue && fields.isEmpty() && doc.isObject())
            {
                const QJsonObject obj = doc.object();
                if (obj.size() == 1 && obj.contains(QStringLiteral("value")))
                    fields.insert(QStringLiteral("value"), obj.value(QStringLiteral("value")).toDouble());
            }
            appendMessage(tables, QString::fromStdString(ch->second.topic), fields, t);
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
        if (tables.empty())
        {
            ps::fillError(err, 2, decoded ? "JSON 中没有数值字段" : "MCAP 中没有可读消息");
            return false;
        }

        std::vector<PsChannelData> channels;
        for (auto &kv : tables)
        {
            auto axis = std::make_shared<const std::vector<double>>(kv.second.t);
            for (auto &ck : kv.second.cols)
            {
                ck.second.resize(kv.second.t.size(), std::numeric_limits<double>::quiet_NaN());
                PsChannelData c;
                c.name = kv.first + "." + ck.first;
                c.table = kv.first;
                c.axis = axis;
                c.v = std::move(ck.second);
                c.value_type = PS_VALUE_F64;
                channels.push_back(std::move(c));
            }
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

const PsDataSourceVtable *mcapVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<McapInput>(kManifest);
    return &vt;
}

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_load_mcap()
{
    static const PsExtensionDescriptor desc = {
        PS_EXTENSION_ABI_VERSION,
        kManifest,
        PS_FAMILY_DATA_SOURCE,
        0,
        nullptr,
        nullptr,
        nullptr,
        mcapVtable(),
    };
    return &desc;
}
