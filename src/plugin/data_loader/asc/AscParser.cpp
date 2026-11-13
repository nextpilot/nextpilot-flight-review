#include "AscParser.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string &s)
{
    const size_t lb = s.find_first_not_of(" \t\r\n");
    const size_t rb = s.find_last_not_of(" \t\r\n");
    return lb == std::string::npos ? std::string() : s.substr(lb, rb - lb + 1);
}

bool parseId(const std::string &tok, bool hex, uint32_t &id, bool &ext)
{
    std::string body = tok;
    ext = false;
    if (!body.empty() && (body.back() == 'x' || body.back() == 'X'))
    {
        ext = true;
        body.pop_back();
    }
    if (body.empty())
        return false;
    char *end = nullptr;
    const unsigned long v = std::strtoul(body.c_str(), &end, hex ? 16 : 10);
    if (!end || *end != '\0')
        return false;
    id = static_cast<uint32_t>(v) & 0x1FFFFFFFu;
    return true;
}

bool parseByte(const std::string &tok, bool hex, uint8_t &v)
{
    char *end = nullptr;
    const unsigned long n = std::strtoul(tok.c_str(), &end, hex ? 16 : 10);
    if (!end || *end != '\0' || n > 255)
        return false;
    v = static_cast<uint8_t>(n);
    return true;
}

} // namespace

bool psParseAscFile(const char *path, AscLog &log, std::string &err)
{
    std::string text;
    if (!psReadTextFile(path, text, err))
        return false;
    bool baseHex = true;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        line = trim(line);
        if (nl == std::string::npos)
            start = text.size() + 1;
        else
            start = nl + 1;
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        const std::string low = lowerAscii(line);
        if (low.rfind("base ", 0) == 0)
        {
            baseHex = low.find("hex") != std::string::npos;
            continue;
        }
        if (low.rfind("date ", 0) == 0 || low.rfind("begin triggerblock", 0) == 0 ||
            low.rfind("end triggerblock", 0) == 0 || low.find("internal events") != std::string::npos ||
            low.rfind("no internal events", 0) == 0 || low.rfind("start of measurement", 0) == 0 ||
            low.rfind("errorframe", 0) == 0)
            continue;

        std::istringstream ss(line);
        std::vector<std::string> tok;
        std::string w;
        while (ss >> w)
            tok.push_back(w);
        if (tok.size() < 3)
            continue;
        double ts = 0;
        try
        {
            ts = std::stod(tok[0]);
        }
        catch (...)
        {
            continue;
        }

        if (lowerAscii(tok[1]) == "canfd")
        {
            if (tok.size() < 8)
                continue;
            uint8_t ch = 1;
            try { ch = static_cast<uint8_t>(std::stoul(tok[2])); } catch (...) {}
            const std::string dir = tok[3];
            uint32_t id = 0;
            bool ext = false;
            if (!parseId(tok[4], baseHex, id, ext))
                continue;
            size_t len = 0;
            try { len = static_cast<size_t>(std::stoul(tok[8])); } catch (...) {}
            CanFrame f;
            f.t = ts;
            f.bus = ch;
            f.id = id;
            f.ext = ext;
            f.fd = true;
            f.rx = lowerAscii(dir) == "rx";
            for (size_t i = 8; i < tok.size() && f.data.size() < len; ++i)
            {
                uint8_t b = 0;
                if (!parseByte(tok[i], baseHex, b))
                    break;
                f.data.push_back(b);
            }
            log.frames.push_back(std::move(f));
            continue;
        }

        uint8_t ch = 1;
        try { ch = static_cast<uint8_t>(std::stoul(tok[1])); }
        catch (...) { continue; }
        if (tok.size() < 5)
            continue;
        uint32_t id = 0;
        bool ext = false;
        if (!parseId(tok[2], baseHex, id, ext))
            continue;
        const std::string dir = tok[3];
        const std::string kind = tok[4];
        if (lowerAscii(kind) == "r")
            continue;
        size_t dlc = 0;
        try { dlc = std::min<size_t>(static_cast<size_t>(std::stoul(tok[5])), 8); } catch (...) {}
        CanFrame f;
        f.t = ts;
        f.bus = ch;
        f.id = id;
        f.ext = ext;
        f.fd = false;
        f.rx = lowerAscii(dir) == "rx";
        for (size_t i = 6; i < tok.size() && f.data.size() < dlc; ++i)
        {
            uint8_t b = 0;
            if (!parseByte(tok[i], baseHex, b))
                break;
            f.data.push_back(b);
        }
        log.frames.push_back(std::move(f));
    }
    if (log.frames.empty())
    {
        err = "ASC 里没有解析出任何 CAN 帧（检查是不是符号化导出或纯错误帧）";
        return false;
    }
    return true;
}
