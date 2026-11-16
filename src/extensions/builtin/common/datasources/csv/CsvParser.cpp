#include "CsvParser.h"

#include "PsLoaderCommon.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string trim(const std::string &s)
{
    const size_t lb = s.find_first_not_of(" \t\r\n");
    const size_t rb = s.find_last_not_of(" \t\r\n");
    return lb == std::string::npos ? std::string() : s.substr(lb, rb - lb + 1);
}

std::vector<std::string> splitLine(const std::string &line, char sep)
{
    std::vector<std::string> out;
    std::string cell;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
            {
                cell.push_back('"');
                ++i;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else if (c == sep && !quoted)
        {
            out.push_back(trim(cell));
            cell.clear();
        }
        else
        {
            cell.push_back(c);
        }
    }
    out.push_back(trim(cell));
    return out;
}

struct DelimiterGuess
{
    char delimiter = ',';
    bool ambiguous = false;
};

DelimiterGuess detectDelimiter(const std::string &line)
{
    std::map<char, size_t> counts{{',', 0}, {';', 0}, {'\t', 0}, {' ', 0}};
    bool quoted = false;
    for (char c : line)
    {
        if (c == '"')
        {
            quoted = !quoted;
            continue;
        }
        if (!quoted && counts.count(c))
            ++counts[c];
    }
    size_t hard = 0;
    for (char c : {',', ';', '\t'})
        if (counts[c] > 0)
            ++hard;
    char best = ',';
    size_t bestCount = 0;
    for (const auto &kv : counts)
    {
        if (kv.second == 0)
            continue;
        if (kv.first == ' ' && hard > 0)
            continue;
        if (kv.second > bestCount)
        {
            best = kv.first;
            bestCount = kv.second;
        }
    }
    return {best, hard >= 2};
}

int64_t daysFromCivil(int64_t y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t mp = static_cast<uint32_t>(m > 2 ? m - 3 : m + 9);
    const uint32_t doy = (153 * mp + 2) / 5 + static_cast<uint32_t>(d) - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool parseDate(const std::string &s, int64_t &seconds)
{
    std::string t = trim(s);
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= t.size())
    {
        const size_t p = t.find_first_of("-/", start);
        parts.push_back(t.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos)
            break;
        start = p + 1;
    }
    if (parts.size() != 3)
        return false;
    try
    {
        const int64_t y = std::stoll(parts[0]);
        const int m = std::stoi(parts[1]);
        const int d = std::stoi(parts[2]);
        if (m < 1 || m > 12 || d < 1 || d > 31)
            return false;
        seconds = daysFromCivil(y, m, d) * 86400;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseTimeOfDay(const std::string &s, double &seconds)
{
    const std::string t = trim(s);
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= t.size())
    {
        const size_t p = t.find(':', start);
        parts.push_back(t.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos)
            break;
        start = p + 1;
    }
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    try
    {
        const double h = std::stod(parts[0]);
        const double m = std::stod(parts[1]);
        const double sec = parts.size() == 3 ? std::stod(parts[2]) : 0.0;
        if (h < 0 || h >= 24 || m < 0 || m >= 60 || sec < 0 || sec >= 61)
            return false;
        seconds = h * 3600 + m * 60 + sec;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseDateTimeText(const std::string &s, double &seconds)
{
    std::string t = trim(s);
    if (!t.empty() && (t.back() == 'Z' || t.back() == 'z'))
        t.pop_back();
    const size_t pos = t.find_first_of("Tt");
    if (pos != std::string::npos)
    {
        int64_t days = 0;
        double tod = 0;
        if (!parseDate(t.substr(0, pos), days) || !parseTimeOfDay(t.substr(pos + 1), tod))
            return false;
        seconds = static_cast<double>(days) + tod;
        return true;
    }
    const size_t space = t.find(' ');
    if (space != std::string::npos)
    {
        int64_t days = 0;
        double tod = 0;
        if (parseDate(t.substr(0, space), days) && parseTimeOfDay(t.substr(space + 1), tod))
        {
            seconds = static_cast<double>(days) + tod;
            return true;
        }
    }
    return parseTimeOfDay(t, seconds);
}

bool isNumeric(const std::string &s)
{
    if (trim(s).empty())
        return false;
    char *end = nullptr;
    std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

double parseNumber(const std::string &s)
{
    if (trim(s).empty())
        return kNaN;
    char *end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end && *end == '\0' ? v : kNaN;
}

std::string cellKind(const std::string &s)
{
    const std::string t = trim(s);
    if (t.empty() || isNumeric(t))
        return "numeric";
    int64_t days = 0;
    if (parseDate(t, days))
        return "date_only";
    double sec = 0;
    if (parseTimeOfDay(t, sec))
        return "time_only";
    if (parseDateTimeText(t, sec))
        return "numeric";
    return "text";
}

double headerUnit(const std::string &name)
{
    std::string h = name;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (h.find("ns") != std::string::npos) return 1e-9;
    if (h.find("us") != std::string::npos || h.find("µs") != std::string::npos) return 1e-6;
    if (h.find("ms") != std::string::npos) return 1e-3;
    if (h.find("sec") != std::string::npos) return 1.0;
    return 0.0;
}

double numericTimeScale(const std::vector<double> &values, const std::string &header)
{
    const double hinted = headerUnit(header);
    if (hinted > 0)
        return hinted;
    double maxv = 0;
    for (double v : values)
        if (std::isfinite(v))
            maxv = std::max(maxv, std::abs(v));
    if (maxv >= 1e17) return 1e-9;
    if (maxv >= 1e14) return 1e-6;
    if (maxv >= 1e11) return 1e-3;
    return 1.0;
}

void dedupeNames(std::vector<std::string> &names)
{
    std::map<std::string, int> seen;
    for (auto &n : names)
    {
        const std::string base = trim(n).empty() ? "Column" : trim(n);
        int &count = seen[base];
        ++count;
        n = count > 1 ? base + "_" + std::to_string(count) : base;
    }
}

std::vector<std::string> nonEmptyLines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!trim(line).empty())
            lines.push_back(line);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return lines;
}

struct TimeBuild
{
    std::vector<double> t;
    std::string name;
    double scale = 1.0;
    size_t bad = 0;
};

TimeBuild buildTime(const std::vector<std::vector<std::string>> &rows,
                    const std::vector<std::string> &headers, const CsvOptions &opt)
{
    TimeBuild out;
    if (opt.timeMode == 1)
    {
        const size_t c = static_cast<size_t>(opt.timeIndex);
        out.name = c < headers.size() ? headers[c] : "Column " + std::to_string(c + 1);
        bool allNumeric = true;
        for (const auto &row : rows)
        {
            const std::string cell = c < row.size() ? row[c] : std::string();
            if (!trim(cell).empty() && !isNumeric(cell))
                allNumeric = false;
            double sec = 0;
            if (parseDateTimeText(cell, sec))
                out.t.push_back(sec);
            else if (isNumeric(cell))
                out.t.push_back(parseNumber(cell));
            else
            {
                out.t.push_back(kNaN);
                ++out.bad;
            }
        }
        if (allNumeric)
        {
            out.scale = numericTimeScale(out.t, out.name);
            for (double &v : out.t)
                v *= out.scale;
        }
        else
        {
            out.bad = 0;
        }
        return out;
    }
    if (opt.timeMode == 2)
    {
        const size_t d = static_cast<size_t>(opt.dateIndex);
        const size_t tm = static_cast<size_t>(opt.clockIndex);
        const std::string dn = d < headers.size() ? headers[d] : "date";
        const std::string tn = tm < headers.size() ? headers[tm] : "time";
        out.name = dn + " + " + tn;
        for (const auto &row : rows)
        {
            const std::string ds = d < row.size() ? row[d] : std::string();
            const std::string ts = tm < row.size() ? row[tm] : std::string();
            int64_t days = 0;
            double tod = 0;
            if (parseDate(ds, days) && parseTimeOfDay(ts, tod))
                out.t.push_back(static_cast<double>(days) + tod);
            else
            {
                out.t.push_back(kNaN);
                ++out.bad;
            }
        }
        return out;
    }
    out.name = "row #";
    out.t.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
        out.t[i] = static_cast<double>(i);
    return out;
}

} // namespace

bool psCsvProbeFile(const char *path, char forcedDelimiter, CsvProbe &out, std::string &err)
{
    std::string text;
    if (!psReadTextFile(path, text, err))
        return false;
    psLoaderProgress(10, 100);
    if (text.size() >= 3 && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF')
        text.erase(0, 3);
    const std::vector<std::string> lines = nonEmptyLines(text);
    if (lines.empty())
    {
        err = "空文件";
        return false;
    }
    DelimiterGuess guess = detectDelimiter(lines[0]);
    if (forcedDelimiter != 0)
    {
        guess.delimiter = forcedDelimiter;
        guess.ambiguous = false;
    }
    const std::vector<std::string> parts = splitLine(lines[0], guess.delimiter);
    if (parts.size() < 2)
    {
        err = "只切出 " + std::to_string(parts.size()) + " 列，分隔符可能选错了";
        return false;
    }
    bool hasHeader = false;
    for (const auto &p : parts)
        if (!p.empty() && cellKind(p) == "text")
            hasHeader = true;

    std::vector<std::string> names = hasHeader
        ? parts
        : [&]() {
            std::vector<std::string> n;
            for (size_t i = 1; i <= parts.size(); ++i)
                n.push_back("Column " + std::to_string(i));
            return n;
        }();
    dedupeNames(names);

    std::vector<std::vector<std::string>> preview;
    if (!hasHeader)
        preview.push_back(parts);
    for (size_t i = 1; i < lines.size() && preview.size() < 9; ++i)
        preview.push_back(splitLine(lines[i], guess.delimiter));

    out = {};
    out.delimiter = guess.delimiter;
    out.ambiguous = guess.ambiguous;
    out.hasHeader = hasHeader;
    out.preview = preview;
    out.columns.resize(names.size());
    for (size_t ci = 0; ci < names.size(); ++ci)
    {
        size_t textVotes = 0, dateVotes = 0, timeVotes = 0;
        for (const auto &row : preview)
        {
            if (ci >= row.size())
                continue;
            const std::string k = cellKind(row[ci]);
            if (k == "text") ++textVotes;
            else if (k == "date_only") ++dateVotes;
            else if (k == "time_only") ++timeVotes;
        }
        out.columns[ci].name = names[ci];
        out.columns[ci].kind = textVotes > 0 ? "text"
            : dateVotes > 0 ? "date_only"
            : timeVotes > 0 ? "time_only"
            : "numeric";
    }

    for (size_t i = 0; i < names.size(); ++i)
    {
        std::string lower = names[i];
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool timeName = lower.find("time") != std::string::npos ||
                              lower.find("stamp") != std::string::npos ||
                              lower == "t" || lower.find("时间") != std::string::npos;
        if (out.columns[i].kind == "numeric" && timeName)
        {
            out.suggestedTime = static_cast<int>(i);
            break;
        }
    }
    if (out.suggestedTime < 0 && names.size() > 1 && out.columns[0].kind == "numeric")
        out.suggestedTime = 0;

    if (guess.ambiguous)
        out.warnings.push_back("首行里出现两种以上硬分隔符，自动探测不可靠，请手动选");
    if (!hasHeader)
        out.warnings.push_back("首行看起来是数据而不是表头，已自动生成列名（可手动改）");
    size_t mismatch = 0;
    for (const auto &row : preview)
        if (row.size() != names.size())
            ++mismatch;
    if (mismatch > 0)
        out.warnings.push_back("预览中有 " + std::to_string(mismatch) + " 行列数不是 " +
                               std::to_string(names.size()) + "，加载时会跳过");
    return true;
}

bool psCsvParseFile(const char *path, const CsvOptions &opt, CsvTable &out, std::string &err)
{
    std::string text;
    if (!psReadTextFile(path, text, err))
        return false;
    psLoaderProgress(10, 100);
    if (text.size() >= 3 && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF')
        text.erase(0, 3);

    std::vector<std::string> lines = nonEmptyLines(text);
    psLoaderProgress(12, 100);
    if (opt.skipRows > 0)
    {
        const size_t skip = std::min<size_t>(static_cast<size_t>(opt.skipRows), lines.size());
        lines.erase(lines.begin(), lines.begin() + skip);
    }
    if (lines.empty())
    {
        err = "空文件";
        return false;
    }

    std::vector<std::string> headers;
    size_t dataStart = 0;
    if (opt.hasHeader)
    {
        headers = splitLine(lines[0], opt.delimiter);
        dedupeNames(headers);
        dataStart = 1;
    }

    std::vector<std::vector<std::string>> raw;
    for (size_t i = dataStart; i < lines.size(); ++i)
    {
        raw.push_back(splitLine(lines[i], opt.delimiter));
        psLoaderProgress(12 + (i - dataStart + 1) * 33 / std::max<size_t>(1, lines.size() - dataStart), 100);
    }
    if (headers.empty())
    {
        size_t maxCols = 0;
        for (const auto &r : raw)
            maxCols = std::max(maxCols, r.size());
        for (size_t i = 1; i <= maxCols; ++i)
            headers.push_back("Column " + std::to_string(i));
    }
    const size_t nCols = headers.size();

    std::vector<std::vector<std::string>> kept;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (raw[i].size() == nCols)
            kept.push_back(raw[i]);
        psLoaderProgress(45 + (i + 1) * 10 / std::max<size_t>(1, raw.size()), 100);
    }
    if (kept.empty())
    {
        err = "没有一条数据行可用";
        return false;
    }

    psLoaderProgress(55, 100);
    TimeBuild tb = buildTime(kept, headers, opt);
    psLoaderProgress(65, 100);
    std::vector<const std::vector<std::string> *> rowsOk;
    std::vector<double> tOk;
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (std::isfinite(tb.t[i]))
        {
            rowsOk.push_back(&kept[i]);
            tOk.push_back(tb.t[i]);
        }
        psLoaderProgress(65 + (i + 1) * 10 / std::max<size_t>(1, kept.size()), 100);
    }
    if (rowsOk.empty())
    {
        err = "时间列没有一条是合法数值";
        return false;
    }

    psLoaderProgress(78, 100);
    std::vector<size_t> idx(tOk.size());
    for (size_t i = 0; i < idx.size(); ++i)
        idx[i] = i;
    if (!std::is_sorted(tOk.begin(), tOk.end()))
    {
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return tOk[a] < tOk[b]; });
    }

    std::set<size_t> skip;
    if (opt.timeMode == 1)
        skip.insert(static_cast<size_t>(opt.timeIndex));
    else if (opt.timeMode == 2)
    {
        skip.insert(static_cast<size_t>(opt.dateIndex));
        skip.insert(static_cast<size_t>(opt.clockIndex));
    }

    out = {};
    out.headers.push_back(tb.name);
    std::vector<double> tOut;
    tOut.reserve(idx.size());
    size_t outDone = 0;
    for (size_t i : idx)
        tOut.push_back(tOk[i]);
    out.columns.push_back(std::move(tOut));
    for (size_t c = 0; c < nCols; ++c)
    {
        if (skip.count(c))
            continue;
        std::vector<double> v;
        v.reserve(idx.size());
        for (size_t i : idx)
            v.push_back(parseNumber((*rowsOk[i])[c]));
        out.headers.push_back(headers[c]);
        out.columns.push_back(std::move(v));
        psLoaderProgress(80 + static_cast<uint64_t>(c + 1) * 20 / std::max<size_t>(1, nCols), 100);
    }
    return true;
}

