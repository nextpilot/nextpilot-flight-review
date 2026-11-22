#include "XlsxBook.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVector>

namespace nextpilot::plotscope::host
{
namespace
{

// --- 列号 → 单元格引用（A, B, …, Z, AA…）---
QString columnRef(int index)
{
    QString out;
    int n = index;
    while (n >= 0)
    {
        out.prepend(QChar('A' + (n % 26)));
        n = n / 26 - 1;
    }
    return out;
}

QString xmlEscape(const QString &src)
{
    QString out = src;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    out.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    // XML 1.0 不接受控制字符（\t \n \r 除外），去掉以免 Excel 报文件损坏。
    QString cleaned;
    cleaned.reserve(out.size());
    for (const QChar ch : out)
    {
        const unsigned short c = ch.unicode();
        if (c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D)
            continue;
        cleaned.append(ch);
    }
    return cleaned;
}

// --- 最小 zip（store only）---
struct ZipEntry
{
    QString name;
    QByteArray data;
    quint32 crc = 0;
    quint32 offset = 0;
};

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init)
    {
        for (quint32 i = 0; i < 256; ++i)
        {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (char b : data)
        crc = table[(crc ^ static_cast<unsigned char>(b)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void putU16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void putU32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

// 固定时间戳（1980-01-01），避免同一份数据两次导出字节不同（diff 友好）。
constexpr quint16 kDosTime = 0;
constexpr quint16 kDosDate = static_cast<quint16>(((1980 - 1980) << 9) | (1 << 5) | 1);

bool writeZip(const QString &path, QVector<ZipEntry> &entries, QString *err)
{
    QByteArray out;
    for (ZipEntry &e : entries)
    {
        e.crc = crc32(e.data);
        e.offset = quint32(out.size());
        putU32(out, 0x04034b50u);           // local file header
        putU16(out, 20);                    // version needed
        putU16(out, 0);                     // flags
        putU16(out, 0);                     // method = store
        putU16(out, kDosTime);
        putU16(out, kDosDate);
        putU32(out, e.crc);
        putU32(out, quint32(e.data.size())); // compressed size
        putU32(out, quint32(e.data.size())); // uncompressed size
        putU16(out, quint16(e.name.size()));
        putU16(out, 0);                     // extra len
        out.append(e.name.toUtf8());
        out.append(e.data);
    }
    const quint32 cdStart = quint32(out.size());
    for (const ZipEntry &e : entries)
    {
        putU32(out, 0x02014b50u);           // central directory
        putU16(out, 20);
        putU16(out, 20);
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, kDosTime);
        putU16(out, kDosDate);
        putU32(out, e.crc);
        putU32(out, quint32(e.data.size()));
        putU32(out, quint32(e.data.size()));
        putU16(out, quint16(e.name.size()));
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, 0);
        putU32(out, 0);
        putU32(out, e.offset);
        out.append(e.name.toUtf8());
    }
    const quint32 cdSize = quint32(out.size()) - cdStart;
    putU32(out, 0x06054b50u);               // end of central directory
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, quint16(entries.size()));
    putU16(out, quint16(entries.size()));
    putU32(out, cdSize);
    putU32(out, cdStart);
    putU16(out, 0);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (err)
            *err = QStringLiteral("无法写入: %1").arg(path);
        return false;
    }
    if (f.write(out) != out.size())
    {
        if (err)
            *err = QStringLiteral("写入中断: %1").arg(path);
        return false;
    }
    f.close();
    return true;
}

QByteArray sheetXml(const XlsxBook::Sheet &sheet)
{
    QByteArray xml;
    xml.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    xml.append("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    xml.append("<sheetData>");
    auto appendRow = [&xml](int rowIndex, const QVector<XlsxBook::Cell> &cells) {
        xml.append("<row r=\"").append(QByteArray::number(rowIndex)).append("\">");
        for (int c = 0; c < cells.size(); ++c)
        {
            const XlsxBook::Cell &cell = cells.at(c);
            if (!cell.isNumber && cell.text.isEmpty())
                continue; // 空文本不占格（Excel 视为空单元格）
            xml.append("<c r=\"")
                .append(columnRef(c).toUtf8())
                .append(QByteArray::number(rowIndex))
                .append("\"");
            if (cell.isNumber)
                xml.append("><v>")
                    .append(QByteArray::number(cell.number, 'g', 15))
                    .append("</v></c>");
            else
                xml.append(" t=\"inlineStr\"><is><t>")
                    .append(xmlEscape(cell.text).toUtf8())
                    .append("</t></is></c>");
        }
        xml.append("</row>");
    };
    QVector<XlsxBook::Cell> headerCells;
    headerCells.reserve(sheet.headers.size());
    for (const QString &h : sheet.headers)
        headerCells.append(XlsxBook::Cell::ofText(h));
    if (!headerCells.isEmpty())
        appendRow(1, headerCells);
    for (int i = 0; i < sheet.rows.size(); ++i)
        appendRow(i + 2, sheet.rows.at(i));
    xml.append("</sheetData></worksheet>");
    return xml;
}

QString sanitizeSheetName(const QString &raw, const QStringList &taken)
{
    QString name = raw;
    for (const QChar &bad : QStringLiteral("[]:*?/\\"))
        name.remove(bad);
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("Sheet");
    if (name.size() > 31)
        name = name.left(31);
    QString candidate = name;
    int n = 2;
    while (taken.contains(candidate, Qt::CaseInsensitive))
        candidate = QStringLiteral("%1_%2").arg(name.left(28)).arg(n++);
    return candidate;
}

} // namespace

XlsxBook::Sheet &XlsxBook::addSheet(const QString &name)
{
    QStringList taken;
    for (const Sheet &s : m_sheets)
        taken << s.name;
    Sheet sheet;
    sheet.name = sanitizeSheetName(name, taken);
    m_sheets.append(sheet);
    return m_sheets.last();
}

bool XlsxBook::save(const QString &path, QString *err)
{
    if (m_sheets.isEmpty())
    {
        if (err)
            *err = QStringLiteral("没有 sheet 可写");
        return false;
    }
    QVector<ZipEntry> entries;

    // [Content_Types].xml
    QString types = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>");
    for (int i = 0; i < m_sheets.size(); ++i)
        types += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                                "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                     .arg(i + 1);
    types += QStringLiteral("</Types>");
    entries.append({QStringLiteral("[Content_Types].xml"), types.toUtf8()});

    entries.append({QStringLiteral("_rels/.rels"),
                    QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                               "<Relationship Id=\"rId1\" "
                               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
                               "Target=\"xl/workbook.xml\"/>"
                               "</Relationships>")});

    QString workbook = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>");
    for (int i = 0; i < m_sheets.size(); ++i)
        workbook += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%3\"/>")
                        .arg(xmlEscape(m_sheets.at(i).name))
                        .arg(i + 1)
                        .arg(i + 1);
    workbook += QStringLiteral("</sheets></workbook>");
    entries.append({QStringLiteral("xl/workbook.xml"), workbook.toUtf8()});

    QString rels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 0; i < m_sheets.size(); ++i)
        rels += QStringLiteral("<Relationship Id=\"rId%1\" "
                               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                               "Target=\"worksheets/sheet%2.xml\"/>")
                    .arg(i + 1)
                    .arg(i + 1);
    rels += QStringLiteral("</Relationships>");
    entries.append({QStringLiteral("xl/_rels/workbook.xml.rels"), rels.toUtf8()});

    for (int i = 0; i < m_sheets.size(); ++i)
        entries.append({QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1),
                        sheetXml(m_sheets.at(i))});

    QFileInfo info(path);
    if (!info.absoluteDir().exists())
    {
        if (err)
            *err = QStringLiteral("目录不存在: %1").arg(info.absolutePath());
        return false;
    }
    return writeZip(path, entries, err);
}

} // namespace nextpilot::plotscope::host
