#include "PsLoaderCommon.h"

#include <cmath>
#include <map>

namespace {
thread_local const PsHostApi *t_host = nullptr;
thread_local uint32_t t_lastPct = 0xFFFFFFFFu;
} // namespace

void psLoaderSetHost(const PsHostApi *host)
{
    t_host = host;
    t_lastPct = 0xFFFFFFFFu;
}

void psLoaderProgress(uint64_t done, uint64_t total)
{
    if (!t_host || !t_host->progress || total == 0)
        return;
    uint32_t pct = static_cast<uint32_t>(done * 100 / total);
    if (pct > 100)
        pct = 100;
    if (pct == t_lastPct)
        return;
    t_lastPct = pct;
    t_host->progress(done, total);
}

#include <algorithm>
#include <cstring>
#include <fstream>

void psWriteErr(char *err, uint32_t cap, const std::string &msg)
{
    if (!err || cap == 0)
        return;
    const size_t n = std::min<size_t>(msg.size(), cap - 1);
    std::memcpy(err, msg.data(), n);
    err[n] = '\0';
}

int32_t psWriteJson(const QByteArray &json, char *out, uint32_t cap)
{
    if (!out || cap == 0 || static_cast<uint32_t>(json.size()) >= cap)
        return -1;
    std::memcpy(out, json.constData(), static_cast<size_t>(json.size()));
    out[json.size()] = '\0';
    return static_cast<int32_t>(json.size());
}

int32_t psWriteText(const std::string &text, char *out, uint32_t cap)
{
    if (!out || cap == 0 || static_cast<uint32_t>(text.size()) >= cap)
        return -1;
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return static_cast<int32_t>(text.size());
}

bool psReadFile(const char *path, std::vector<uint8_t> &buf, std::string &err)
{
    if (!path)
    {
        err = "路径为空";
        return false;
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        err = "读取失败：" + std::string(path);
        return false;
    }
    const std::streamoff size = f.tellg();
    if (size < 0)
    {
        err = "读取失败：" + std::string(path);
        return false;
    }
    f.seekg(0, std::ios::beg);
    buf.resize(static_cast<size_t>(size));
    size_t done = 0;
    constexpr size_t kChunk = 1u << 20;
    while (done < buf.size())
    {
        const size_t n = std::min(kChunk, buf.size() - done);
        f.read(reinterpret_cast<char *>(buf.data() + done), static_cast<std::streamsize>(n));
        const size_t got = static_cast<size_t>(f.gcount());
        done += got;
        psLoaderProgress(done * 10 / std::max<size_t>(1, buf.size()), 100);
        if (got < n)
            break;
    }
    if (!f && !f.eof())
    {
        err = "读取失败：" + std::string(path);
        return false;
    }
    buf.resize(done);
    if (!buf.empty())
        psLoaderProgress(10, 100);
    return true;
}

bool psReadTextFile(const char *path, std::string &text, std::string &err)
{
    std::vector<uint8_t> buf;
    if (!psReadFile(path, buf, err))
        return false;
    text.assign(reinterpret_cast<const char *>(buf.data()), buf.size());
    return true;
}

std::string psUnitFromHeader(const std::string &header)
{
    const size_t lb = header.find('[');
    const size_t rb = lb == std::string::npos ? std::string::npos : header.find(']', lb);
    if (lb != std::string::npos && rb != std::string::npos)
        return header.substr(lb + 1, rb - lb - 1);
    const size_t lp = header.find('(');
    const size_t rp = lp == std::string::npos ? std::string::npos : header.find(')', lp);
    if (lp != std::string::npos && rp != std::string::npos)
        return header.substr(lp + 1, rp - lp - 1);
    return {};
}

PsDataset *psBuildDataset(std::vector<PsChannelData> &channels, std::string &err,
                          const PsHostApi *host)
{
    if (!host || host->abi_version != PS_HOST_API_VERSION)
    {
        err = "宿主 API 版本不匹配";
        return nullptr;
    }
    void *raw = host->dataset_new();
    PsDataset *ds = static_cast<PsDataset *>(raw);
    if (!ds)
    {
        err = "数据集分配失败";
        return nullptr;
    }
    char e[256] = {0};
    std::map<std::string, const void *> tables;
    auto tableColumn = [](const PsChannelData &c) {
        const std::string prefix = c.table + ".";
        if (!c.table.empty() && c.name.compare(0, prefix.size(), prefix) == 0)
            return c.name.substr(prefix.size());
        return c.name;
    };
    auto addTypedColumn = [&](PsChannelData &c, uint32_t n) -> int32_t {
        const std::string column = tableColumn(c);
        switch (c.value_type)
        {
        case PS_VALUE_F64:
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_F64, c.v.data(), nullptr, n,
                                                        e, sizeof(e));
        case PS_VALUE_F32:
        {
            std::vector<float> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<float>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_F32, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_I64:
        {
            std::vector<int64_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_I64, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_U64:
        {
            std::vector<uint64_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint64_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_U64, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_I32:
        {
            std::vector<int32_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<int32_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_I32, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_U32:
        {
            std::vector<uint32_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint32_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_U32, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_I16:
        {
            std::vector<int16_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<int16_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_I16, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_U16:
        {
            std::vector<uint16_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint16_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_U16, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_I8:
        {
            std::vector<int8_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<int8_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_I8, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        case PS_VALUE_U8:
        {
            std::vector<uint8_t> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(c.v[i]);
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_U8, v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
        default:
            return host->dataset_add_table_column_typed(raw, c.table.c_str(), column.c_str(),
                                                        c.unit.c_str(), PS_VALUE_F64, c.v.data(), nullptr, n,
                                                        e, sizeof(e));
        }
    };
    for (PsChannelData &c : channels)
    {
        if (c.name.empty())
            continue;
        if (c.axis && !c.table.empty())
        {
            if (c.v.size() != c.axis->size())
            {
                err = c.name + "：点数与时间轴不一致";
                host->dataset_free(raw);
                return nullptr;
            }
            const uint32_t n = static_cast<uint32_t>(c.axis->size());
            auto it = tables.find(c.table);
            if (it == tables.end())
            {
                std::vector<int64_t> time_us(n);
                for (uint32_t i = 0; i < n; ++i)
                    time_us[i] = static_cast<int64_t>(std::llround((*c.axis)[i] * 1e6));
                const int32_t rc = host->dataset_add_table_typed(
                    raw, c.table.c_str(), time_us.data(), n, e, sizeof(e));
                if (rc != 0)
                {
                    err = c.table + "：" + e;
                    host->dataset_free(raw);
                    return nullptr;
                }
                tables.emplace(c.table, c.axis.get());
            }
            else if (it->second != c.axis.get())
            {
                err = c.table + "：同一张表的时间轴不一致";
                host->dataset_free(raw);
                return nullptr;
            }
            const int32_t rc = addTypedColumn(c, n);
            if (rc != 0)
            {
                err = c.name + "：" + e;
                host->dataset_free(raw);
                return nullptr;
            }
            c.v.clear();
            c.v.shrink_to_fit();
            continue;
        }
        const int32_t rc = host->dataset_add_channel(raw, c.name.c_str(), c.unit.c_str(),
                                                     c.t.data(), c.v.data(),
                                                     static_cast<uint32_t>(std::min(c.t.size(), c.v.size())),
                                                     e, sizeof(e));
        if (rc != 0)
        {
            err = c.name + "：" + e;
            host->dataset_free(raw);
            return nullptr;
        }
        c.t.clear();
        c.t.shrink_to_fit();
        c.v.clear();
        c.v.shrink_to_fit();
    }
    return ds;
}
