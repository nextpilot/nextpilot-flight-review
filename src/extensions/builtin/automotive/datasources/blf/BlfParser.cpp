#include "BlfParser.h"
#include "PsLoaderCommon.h"

#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace {

constexpr uint32_t kCanMessage = 1;
constexpr uint32_t kLogContainer = 10;
constexpr uint32_t kCanMessage2 = 86;
constexpr uint32_t kCanFdMessage = 100;
constexpr uint32_t kCanMsgExt = 0x80000000u;

uint16_t u16(const std::vector<uint8_t> &b, size_t o)
{
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}

uint32_t u32(const std::vector<uint8_t> &b, size_t o)
{
    return static_cast<uint32_t>(b[o]) | (uint32_t(b[o + 1]) << 8) |
           (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
}

uint64_t u64(const std::vector<uint8_t> &b, size_t o)
{
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(b[o + i]) << (8 * i);
    return v;
}

size_t nextObject(const std::vector<uint8_t> &b, size_t from)
{
    const size_t end = std::min(from + 16, b.size());
    for (size_t i = from; i + 4 <= end; ++i)
        if (b[i] == 'L' && b[i + 1] == 'O' && b[i + 2] == 'B' && b[i + 3] == 'J')
            return i;
    return from;
}

bool inflateData(const uint8_t *data, size_t len, std::vector<uint8_t> &out)
{
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK)
        return false;
    zs.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(data));
    zs.avail_in = static_cast<uInt>(len);
    out.clear();
    size_t produced = 0;
    int ret = Z_OK;
    while (ret == Z_OK || ret == Z_BUF_ERROR)
    {
        const size_t chunk = 1 << 16;
        out.resize(out.size() + chunk);
        zs.next_out = out.data() + produced;
        zs.avail_out = static_cast<uInt>(chunk);
        ret = inflate(&zs, Z_NO_FLUSH);
        produced += chunk - zs.avail_out;
        if (ret == Z_STREAM_END)
            break;
        if (ret != Z_OK && ret != Z_BUF_ERROR)
            break;
        if (zs.avail_in == 0 && zs.avail_out != 0)
            break;
    }
    inflateEnd(&zs);
    out.resize(produced);
    return ret == Z_STREAM_END;
}

void parseOne(const std::vector<uint8_t> &buf, size_t pos, std::vector<CanFrame> &frames)
{
    const size_t hdrSize = u16(buf, pos + 4);
    const size_t objSize = u32(buf, pos + 8);
    const uint32_t objType = u32(buf, pos + 12);
    if (hdrSize < 32 || pos + hdrSize > buf.size() || pos + objSize > buf.size())
        return;
    const uint32_t flags = u32(buf, pos + 16);
    const uint64_t tsRaw = u64(buf, pos + 24);
    const double t = (flags & 0x2) ? static_cast<double>(tsRaw) * 1e-9
                                   : static_cast<double>(tsRaw) * 1e-5;
    const size_t body = pos + hdrSize;
    if (body + 8 > buf.size())
        return;
    const uint8_t bus = static_cast<uint8_t>(u16(buf, body));
    const uint8_t msgFlags = buf[body + 2];
    const uint8_t dlc = buf[body + 3];
    const uint32_t idRaw = u32(buf, body + 4);

    bool fd = false;
    size_t dataLen = 0, dataStart = 0;
    if (objType == kCanFdMessage)
    {
        fd = true;
        dataLen = body + 24 < buf.size() ? std::min<size_t>(buf[body + 24], 64) : 0;
        dataStart = body + 32;
    }
    else
    {
        dataLen = std::min<size_t>(dlc, 8);
        dataStart = body + 8;
    }
    CanFrame f;
    f.t = t;
    f.bus = bus;
    f.id = idRaw & 0x1FFFFFFFu;
    f.ext = (idRaw & kCanMsgExt) != 0;
    f.fd = fd;
    f.rx = (msgFlags & 0x1) != 0;
    for (size_t i = 0; i < dataLen && dataStart + i < buf.size(); ++i)
        f.data.push_back(buf[dataStart + i]);
    frames.push_back(std::move(f));
}

void drainObjects(std::vector<uint8_t> &pending, std::vector<CanFrame> &frames)
{
    size_t pos = 0;
    while (pos + 16 <= pending.size())
    {
        if (!(pending[pos] == 'L' && pending[pos + 1] == 'O' && pending[pos + 2] == 'B' &&
              pending[pos + 3] == 'J'))
        {
            ++pos;
            continue;
        }
        const size_t objSize = u32(pending, pos + 8);
        if (objSize < 16 || pos + objSize > pending.size())
            break;
        const uint32_t ty = u32(pending, pos + 12);
        if (ty == kCanMessage || ty == kCanMessage2 || ty == kCanFdMessage)
            parseOne(pending, pos, frames);
        pos = nextObject(pending, pos + objSize);
    }
    if (pos > 0)
        pending.erase(pending.begin(), pending.begin() + pos);
}

} // namespace

bool psParseBlfFile(const char *path, BlfLog &log, std::string &err)
{
    std::vector<uint8_t> buf;
    if (!psReadFile(path, buf, err))
        return false;
    if (buf.size() < 144 || !(buf[0] == 'L' && buf[1] == 'O' && buf[2] == 'G' && buf[3] == 'G'))
    {
        err = "不是 BLF：文件头没有 LOGG 签名";
        return false;
    }
    const size_t headerSize = std::max<size_t>(u32(buf, 4), 144);
    std::vector<uint8_t> pending;
    size_t pos = headerSize;
    while (pos + 16 <= buf.size())
    {
        if (!(buf[pos] == 'L' && buf[pos + 1] == 'O' && buf[pos + 2] == 'B' && buf[pos + 3] == 'J'))
        {
            ++pos;
            continue;
        }
        const size_t objSize = u32(buf, pos + 8);
        const uint32_t objType = u32(buf, pos + 12);
        if (objSize < 16 || pos + objSize > buf.size())
            break;
        const size_t objHdrSize = u16(buf, pos + 4);
        if (objHdrSize > objSize)
            break;
        const size_t body = pos + objHdrSize;
        if (objType == kLogContainer)
        {
            if (body + 16 <= pos + objSize)
            {
                const uint16_t method = u16(buf, body);
                const uint8_t *payload = buf.data() + body + 16;
                const size_t payloadLen = pos + objSize - (body + 16);
                if (method == 0)
                    pending.insert(pending.end(), payload, payload + payloadLen);
                else if (method == 2)
                {
                    std::vector<uint8_t> out;
                    if (inflateData(payload, payloadLen, out))
                        pending.insert(pending.end(), out.begin(), out.end());
                }
                drainObjects(pending, log.frames);
            }
        }
        else if (objType == kCanMessage || objType == kCanMessage2 || objType == kCanFdMessage)
        {
            parseOne(buf, pos, log.frames);
        }
        pos = nextObject(buf, pos + objSize);
    }
    if (log.frames.empty())
    {
        err = "BLF 里没找到 CAN 报文";
        return false;
    }
    return true;
}
