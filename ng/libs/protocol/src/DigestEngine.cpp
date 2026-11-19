#include "protocol/DigestEngine.h"

#include <algorithm>
#include <cctype>

namespace nextpilot::plotscope::protocol
{

namespace
{

struct CrcSpec
{
    uint32_t poly = 0;
    uint32_t init = 0;
    uint32_t xorout = 0;
    int width = 8;
    bool refin = false;
    bool refout = false;
};

uint32_t reflectBits(uint32_t v, int bits)
{
    uint32_t r = 0;
    for (int i = 0; i < bits; ++i)
    {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

uint32_t crcGeneric(const uint8_t *p, size_t n, const CrcSpec &c)
{
    const uint32_t top = (c.width == 64) ? 0x80000000u : (1u << (c.width - 1));
    const uint32_t mask = (c.width == 32) ? 0xFFFFFFFFu : ((1u << c.width) - 1u);
    uint32_t crc = c.init & mask;
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t byte = p[i];
        if (c.refin)
            byte = reflectBits(byte, 8);
        crc ^= (byte << (c.width - 8));
        for (int b = 0; b < 8; ++b)
        {
            const bool hi = (crc & top) != 0;
            crc = (crc << 1) & mask;
            if (hi)
                crc ^= (c.poly & mask);
        }
    }
    if (c.refout)
        crc = reflectBits(crc, c.width);
    return (crc ^ c.xorout) & mask;
}

bool lookupAlg(const std::string &name, CrcSpec &crc, int &kind)
{
    // kind: 0 = crc, 1 = xor8, 2 = sum8
    const std::string n = name;
    if (n == "xor8")
    {
        kind = 1;
        return true;
    }
    if (n == "sum8")
    {
        kind = 2;
        return true;
    }
    kind = 0;
    if (n == "crc8")
    {
        crc = {0x07, 0x00, 0x00, 8, false, false};
        return true;
    }
    if (n == "crc16") // CCITT-FALSE
    {
        crc = {0x1021, 0xFFFF, 0x0000, 16, false, false};
        return true;
    }
    if (n == "crc16_modbus")
    {
        crc = {0x8005, 0xFFFF, 0x0000, 16, true, true};
        return true;
    }
    if (n == "crc16_x25")
    {
        crc = {0x1021, 0xFFFF, 0xFFFF, 16, true, true};
        return true;
    }
    if (n == "crc24q")
    {
        crc = {0x864CFB, 0xB704CE, 0x000000, 24, false, false};
        return true;
    }
    if (n == "crc32")
    {
        crc = {0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, 32, true, true};
        return true;
    }
    if (n == "crc32c")
    {
        crc = {0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, 32, true, true};
        return true;
    }
    return false;
}

} // namespace

QStringList digestAlgorithmNames()
{
    return {QStringLiteral("xor8"),   QStringLiteral("sum8"),        QStringLiteral("crc8"),
            QStringLiteral("crc16"),  QStringLiteral("crc16_modbus"), QStringLiteral("crc16_x25"),
            QStringLiteral("crc24q"), QStringLiteral("crc32"),       QStringLiteral("crc32c")};
}

std::string normalizeDigestAlg(const std::string &alg)
{
    std::string a = alg;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
    // NMEA 的校验就是"$ 之后到 * 之前"整段字节的 XOR8；别名而不是另写一套算法。
    if (a == "nmea_xor" || a == "nmea" || a == "nmea_checksum")
        return "xor8";
    return a;
}

bool digestAlgKnown(const std::string &alg)
{
    if (alg.empty())
        return false;
    CrcSpec crc;
    int kind = 0;
    return lookupAlg(normalizeDigestAlg(alg), crc, kind);
}

bool computeDigest(const std::string &alg, const uint8_t *data, size_t len, uint64_t &out,
                   std::string &err)
{
    const std::string norm = normalizeDigestAlg(alg);
    if (norm.empty())
    {
        err = "digest.alg 为空";
        return false;
    }
    CrcSpec crc;
    int kind = 0;
    if (!lookupAlg(norm, crc, kind))
    {
        err = "未知摘要算子: " + alg;
        return false;
    }
    if (!data && len > 0)
    {
        err = "digest 输入为空";
        return false;
    }
    if (kind == 1)
    {
        uint32_t x = 0;
        for (size_t i = 0; i < len; ++i)
            x ^= data[i];
        out = x & 0xFF;
        return true;
    }
    if (kind == 2)
    {
        uint32_t s = 0;
        for (size_t i = 0; i < len; ++i)
            s += data[i];
        out = s & 0xFF;
        return true;
    }
    // 数据长度不足一个 CRC 宽度没有意义（CRC 是整段字节的函数，不是分组）。
    out = crcGeneric(data, len, crc);
    return true;
}

bool scriptDigestAvailable()
{
    return false; // 脚本后端（Lua 5.4）在下一步接入；当前含 code 的模板按加载期错误处理
}

bool computeScriptDigest(const std::string &code, const ScriptDigestInput &in, uint64_t &out,
                         std::string &err)
{
    (void)code;
    (void)in;
    (void)out;
    err = "脚本后端未启用（脚本摘要需 Lua 5.4 支持）";
    return false;
}

} // namespace nextpilot::plotscope::protocol
