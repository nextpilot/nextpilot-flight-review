#include "Dbc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

uint64_t extractBits(const std::vector<uint8_t> &data, const DbcSignal &sig, bool &ok)
{
    ok = true;
    if (sig.length == 0 || sig.length > 64)
    {
        ok = false;
        return 0;
    }
    uint64_t raw = 0;
    if (sig.littleEndian)
    {
        for (uint32_t i = 0; i < sig.length; ++i)
        {
            const uint32_t bit = sig.start + i;
            const uint32_t byte = bit / 8;
            if (byte >= data.size())
            {
                ok = false;
                return 0;
            }
            if ((data[byte] >> (bit % 8)) & 1u)
                raw |= (uint64_t(1) << i);
        }
    }
    else
    {
        for (uint32_t i = 0; i < sig.length; ++i)
        {
            const uint32_t bit = sig.start + i;
            const uint32_t byte = bit / 8;
            if (byte >= data.size())
            {
                ok = false;
                return 0;
            }
            raw = (raw << 1) | ((data[byte] >> (7 - (bit % 8))) & 1u);
        }
    }
    if (sig.isSigned && sig.length < 64 && ((raw >> (sig.length - 1)) & 1u))
        raw |= (~uint64_t(0) << sig.length);
    return raw;
}

bool parseSignal(const std::string &line, DbcSignal &sig)
{
    // SG_ name [mux] : start|len@order sign (scale,offset) [min|max] "unit"
    const size_t colon = line.find(':');
    if (colon == std::string::npos)
        return false;
    std::string head = trim(line.substr(3, colon - 3));
    std::istringstream hs(head);
    hs >> sig.name;
    if (sig.name.empty())
        return false;

    std::string body = trim(line.substr(colon + 1));
    unsigned start = 0, len = 0;
    char order = 0, sign = 0;
    double scale = 1.0, offset = 0.0;
    if (std::sscanf(body.c_str(), "%u|%u@%c%c (%lf,%lf)", &start, &len, &order, &sign, &scale,
                    &offset) < 6)
        return false;
    sig.start = start;
    sig.length = len;
    sig.littleEndian = order == '1';
    sig.isSigned = sign == '-';
    sig.scale = scale;
    sig.offset = offset;
    const size_t q1 = body.find('"');
    if (q1 != std::string::npos)
    {
        const size_t q2 = body.find('"', q1 + 1);
        if (q2 != std::string::npos)
            sig.unit = body.substr(q1 + 1, q2 - q1 - 1);
    }
    return true;
}

} // namespace

bool DbcDatabase::load(const std::string &path, std::string &err)
{
    std::ifstream in(path);
    if (!in)
    {
        err = "无法打开 DBC：" + path;
        return false;
    }
    messages_.clear();
    DbcMessage *cur = nullptr;
    std::string line;
    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.rfind("BO_ ", 0) == 0)
        {
            DbcMessage m;
            std::string name;
            unsigned dlc = 8;
            const unsigned long rawId = std::strtoul(line.c_str() + 4, nullptr, 10);
            const size_t sp = line.find(' ', 4);
            const size_t colon = line.find(':', sp == std::string::npos ? 4 : sp);
            if (sp != std::string::npos)
            {
                std::istringstream ss(line.substr(sp + 1, colon - sp - 1));
                ss >> name;
            }
            if (colon != std::string::npos)
                std::sscanf(line.c_str() + colon + 1, "%u", &dlc);
            m.extended = (rawId & 0x80000000ul) != 0;
            m.id = static_cast<uint32_t>(rawId & (m.extended ? 0x1FFFFFFFul : 0x7FFul));
            m.name = name.empty() ? std::to_string(m.id) : name;
            m.dlc = dlc;
            messages_.push_back(std::move(m));
            cur = &messages_.back();
        }
        else if (line.rfind("SG_ ", 0) == 0 && cur)
        {
            DbcSignal s;
            if (parseSignal(line, s))
                cur->entries.push_back(std::move(s));
        }
    }
    if (messages_.empty())
    {
        err = "DBC 里没有 BO_ 报文定义";
        return false;
    }
    return true;
}

const DbcMessage *DbcDatabase::find(uint32_t id, bool extended) const
{
    for (const auto &m : messages_)
        if (m.extended == extended && m.id == id)
            return &m;
    return nullptr;
}

std::vector<PsChannelData> psCanFramesToDbcChannels(const std::vector<CanFrame> &frames,
                                                    const DbcDatabase &dbc)
{
    struct Column
    {
        std::string unit;
        std::vector<double> v;
    };
    struct Table
    {
        std::vector<double> t;
        std::map<std::string, Column> cols;
    };
    std::map<std::string, Table> tables;
    if (frames.empty())
        return {};
    const double t0 = frames[0].t;
    for (const auto &f : frames)
    {
        const DbcMessage *m = dbc.find(f.id, f.ext);
        if (!m)
            continue;
        char tableName[120];
        std::snprintf(tableName, sizeof(tableName), "CAN%u.%s",
                      static_cast<unsigned>(f.bus), m->name.c_str());
        Table &table = tables[tableName];
        table.t.push_back(f.t - t0);
        const size_t row = table.t.size();
        for (const auto &s : m->entries)
        {
            bool ok = false;
            const uint64_t raw = extractBits(f.data, s, ok);
            auto &col = table.cols[s.name];
            col.unit = s.unit;
            while (col.v.size() + 1 < row)
                col.v.push_back(std::numeric_limits<double>::quiet_NaN());
            col.v.push_back(ok ? static_cast<double>(raw) * s.scale + s.offset
                               : std::numeric_limits<double>::quiet_NaN());
        }
    }
    std::vector<PsChannelData> out;
    for (auto &kv : tables)
    {
        auto axis = std::make_shared<const std::vector<double>>(kv.second.t);
        for (auto &ck : kv.second.cols)
        {
            PsChannelData c;
            c.name = kv.first + "." + ck.first;
            c.table = kv.first;
            c.axis = axis;
            c.unit = ck.second.unit;
            c.v = std::move(ck.second.v);
            c.value_type = PS_VALUE_F64;
            out.push_back(std::move(c));
        }
    }
    return out;
}

std::string psResolveDbcOption(const char *optsJson, const char *logPath)
{
    if (!optsJson || !*optsJson)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(optsJson));
    if (!doc.isObject())
        return {};
    const QString raw = doc.object().value(QStringLiteral("dbc")).toString().trimmed();
    if (raw.isEmpty())
        return {};
    QFileInfo fi(raw);
    if (fi.isRelative() && logPath && *logPath)
        fi = QFileInfo(QFileInfo(QString::fromUtf8(logPath)).absoluteDir(), raw);
    return fi.absoluteFilePath().toStdString();
}
