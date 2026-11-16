#include "PsLoaderCommon.h"

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

PsDataset *psBuildDataset(const std::vector<PsChannelData> &channels, std::string &err,
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
    for (const PsChannelData &c : channels)
    {
        if (c.name.empty())
            continue;
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
    }
    return ds;
}
