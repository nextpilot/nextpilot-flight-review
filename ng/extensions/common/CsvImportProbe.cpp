#include "CsvImportProbe.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QRegularExpression>

#include <algorithm>

namespace ng::ext
{
namespace {

constexpr int kProbeMaxLines = 64;   // 只读前 64 行，足够探测且内存有界
constexpr int kPreviewRows = 8;      // 预览最多 8 行

QString trim(const QString &s)
{
    return s.trimmed();
}

// 按分隔符切一行（支持双引号包裹与 "" 转义）。
QStringList splitLine(const QString &line, QChar sep)
{
    QStringList out;
    QString cell;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"'))
        {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"'))
            {
                cell.append(QLatin1Char('"'));
                ++i;
            }
            else
                quoted = !quoted;
        }
        else if (ch == sep && !quoted)
        {
            out.append(cell.trimmed());
            cell.clear();
        }
        else
            cell.append(ch);
    }
    out.append(cell.trimmed());
    return out;
}

struct DelimiterGuess
{
    QChar delimiter = QLatin1Char(',');
    bool ambiguous = false;
};

// 从首行猜分隔符：, ; \t 空格 中出现次数最多者；两种以上硬分隔符 = 歧义。
DelimiterGuess detectDelimiter(const QString &line)
{
    QHash<QChar, int> counts;
    const QChar hard[] = {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t')};
    for (QChar c : hard)
        counts.insert(c, 0);
    counts.insert(QLatin1Char(' '), 0);

    bool quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);
        if (c == QLatin1Char('"'))
        {
            quoted = !quoted;
            continue;
        }
        if (!quoted && counts.contains(c))
            counts[c] = counts.value(c) + 1;
    }

    int hardKinds = 0;
    for (QChar c : hard)
        if (counts.value(c) > 0)
            ++hardKinds;

    QChar best = QLatin1Char(',');
    int bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        if (it.value() == 0)
            continue;
        if (it.key() == QLatin1Char(' ') && hardKinds > 0)
            continue;
        if (it.value() > bestCount)
        {
            best = it.key();
            bestCount = it.value();
        }
    }
    return {best, hardKinds >= 2};
}

bool isNumeric(const QString &s)
{
    if (s.trimmed().isEmpty())
        return false;
    bool ok = false;
    s.trimmed().toDouble(&ok);
    return ok;
}

// 单元格类型：numeric / date_only / time_only / text
QString cellKind(const QString &raw)
{
    const QString s = trim(raw);
    if (s.isEmpty() || isNumeric(s))
        return QStringLiteral("numeric");
    static const QRegularExpression dateRe(QStringLiteral("^\\d{4}[-/]\\d{1,2}[-/]\\d{1,2}$"));
    if (dateRe.match(s).hasMatch())
        return QStringLiteral("date_only");
    static const QRegularExpression timeRe(QStringLiteral("^\\d{1,2}:\\d{2}(:\\d{2})?$"));
    if (timeRe.match(s).hasMatch())
        return QStringLiteral("time_only");
    return QStringLiteral("text");
}

QStringList dedupeNames(const QStringList &in)
{
    QStringList out;
    QHash<QString, int> seen;
    for (const QString &raw : in)
    {
        const QString base = raw.trimmed().isEmpty() ? QStringLiteral("Column") : raw.trimmed();
        int &n = seen[base];
        ++n;
        out.append(n > 1 ? base + QStringLiteral("_%1").arg(n) : base);
    }
    return out;
}

} // namespace

QJsonObject CsvImportProbe::probe(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject{{QStringLiteral("error"), QStringLiteral("无法打开文件: %1").arg(path)}};
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    QStringList lines;
    while (!in.atEnd() && lines.size() < kProbeMaxLines)
    {
        QString line = in.readLine();
        if (!line.isEmpty())
            lines.append(line);
    }
    f.close();
    if (lines.isEmpty())
        return QJsonObject{{QStringLiteral("error"), QStringLiteral("空文件")}};

    DelimiterGuess guess = detectDelimiter(lines.first());
    const QStringList firstParts = splitLine(lines.first(), guess.delimiter);
    if (firstParts.size() < 2)
        return QJsonObject{{QStringLiteral("error"),
                            QStringLiteral("只切出 %1 列，分隔符可能选错了").arg(firstParts.size())}};

    // 表头判断：某格非空且是 text → 认为是表头。
    bool hasHeader = false;
    for (const QString &p : firstParts)
        if (!p.isEmpty() && cellKind(p) == QStringLiteral("text"))
            hasHeader = true;

    QStringList names;
    if (hasHeader)
    {
        names = firstParts;
    }
    else
    {
        for (int i = 1; i <= firstParts.size(); ++i)
            names.append(QStringLiteral("Column %1").arg(i));
    }
    names = dedupeNames(names);

    // 预览：无表头时第一行是数据；随后最多取 kPreviewRows 行。
    QJsonArray preview;
    if (!hasHeader)
    {
        QJsonArray r;
        for (const QString &c : firstParts)
            r.append(c);
        preview.append(r);
    }
    for (int i = 1; i < lines.size() && preview.size() < kPreviewRows; ++i)
    {
        QJsonArray r;
        for (const QString &c : splitLine(lines.at(i), guess.delimiter))
            r.append(c);
        preview.append(r);
    }

    // 列类型：对预览每列投票。
    QJsonArray columns;
    for (int ci = 0; ci < names.size(); ++ci)
    {
        int textVotes = 0, dateVotes = 0, timeVotes = 0;
        for (const QJsonValue &row : preview)
        {
            const QJsonArray cells = row.toArray();
            if (ci >= cells.size())
                continue;
            const QString k = cellKind(cells.at(ci).toString());
            if (k == QStringLiteral("text")) ++textVotes;
            else if (k == QStringLiteral("date_only")) ++dateVotes;
            else if (k == QStringLiteral("time_only")) ++timeVotes;
        }
        const QString kind = textVotes > 0 ? QStringLiteral("text")
                           : dateVotes > 0 ? QStringLiteral("date_only")
                           : timeVotes > 0 ? QStringLiteral("time_only")
                                          : QStringLiteral("numeric");
        columns.append(QJsonObject{{QStringLiteral("name"), names.at(ci)},
                                   {QStringLiteral("kind"), kind}});
    }

    // 建议时间列：列名含 time/stamp/t/时间 且是数值列；否则第一数值列。
    int suggestedTime = -1;
    for (int i = 0; i < names.size(); ++i)
    {
        const QString lower = names.at(i).toLower();
        const bool timeName = lower.contains(QStringLiteral("time"))
                           || lower.contains(QStringLiteral("stamp"))
                           || lower == QStringLiteral("t")
                           || names.at(i).contains(QStringLiteral("时间"));
        const QString kind = columns.at(i).toObject().value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("numeric") && timeName)
        {
            suggestedTime = i;
            break;
        }
    }
    if (suggestedTime < 0 && names.size() > 1
        && columns.first().toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("numeric"))
        suggestedTime = 0;

    // 警告。
    QStringList warnings;
    if (guess.ambiguous)
        warnings.append(QStringLiteral("首行出现两种以上硬分隔符，自动探测不可靠，请手动选"));
    if (!hasHeader)
        warnings.append(QStringLiteral("首行看起来是数据而非表头，已自动生成列名"));
    int mismatch = 0;
    for (const QJsonValue &row : preview)
        if (row.toArray().size() != names.size())
            ++mismatch;
    if (mismatch > 0)
        warnings.append(QStringLiteral("预览中有 %1 行列数不是 %2，加载时会按空值处理")
                            .arg(mismatch).arg(names.size()));

    QJsonArray warnsArr;
    for (const QString &w : warnings)
        warnsArr.append(w);

    return QJsonObject{
        {QStringLiteral("delimiter"), QString(guess.delimiter)},
        {QStringLiteral("ambiguous"), guess.ambiguous},
        {QStringLiteral("has_header"), hasHeader},
        {QStringLiteral("columns"), columns},
        {QStringLiteral("preview"), preview},
        {QStringLiteral("suggested_time"), suggestedTime},
        {QStringLiteral("warnings"), warnsArr},
    };
}

QByteArray CsvImportProbe::defaultOptions(const QJsonObject &p)
{
    QJsonObject o;
    o[QStringLiteral("delimiter")] = p.value(QStringLiteral("delimiter")).toString(QStringLiteral(","));
    o[QStringLiteral("has_header")] = p.value(QStringLiteral("has_header")).toBool(true);
    o[QStringLiteral("skip_rows")] = 0;
    const int suggested = p.value(QStringLiteral("suggested_time")).toInt(-1);
    QJsonObject time;
    if (suggested >= 0)
    {
        time[QStringLiteral("mode")] = QStringLiteral("column");
        time[QStringLiteral("index")] = suggested;
    }
    else
    {
        time[QStringLiteral("mode")] = QStringLiteral("row");
    }
    o[QStringLiteral("time")] = time;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace ng::ext
