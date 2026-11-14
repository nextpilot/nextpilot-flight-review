#include "ProtocolEditorDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdint>

namespace {

uint16_t crc16(const QByteArray &data, int length, uint16_t poly, uint16_t init)
{
    uint16_t crc = init;
    for (int i = 0; i < length && i < data.size(); ++i)
    {
        crc ^= static_cast<uint16_t>(static_cast<uint8_t>(data.at(i))) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ poly)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

uint64_t readBits(const QByteArray &frame, uint32_t bitOffset, uint32_t bits)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < bits; ++i)
    {
        const uint32_t abs = bitOffset + i;
        if (abs / 8 >= static_cast<uint32_t>(frame.size()))
            break;
        if (static_cast<uint8_t>(frame.at(abs / 8)) & (1u << (abs % 8)))
            v |= (1ull << i);
    }
    return v;
}

} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QStringLiteral("协议模板编辑器"));
    resize(760, 560);

    auto *root = new QVBoxLayout(this);
    m_json = new QPlainTextEdit(this);
    m_json->setPlaceholderText(QStringLiteral("{\"frame_size\":14,\"sync\":[170,85],...}"));
    root->addWidget(m_json, 1);

    m_fields = new QTableWidget(0, 6, this);
    m_fields->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("字节"),
                                         QStringLiteral("起始位"), QStringLiteral("位宽"),
                                         QStringLiteral("有符号"), QStringLiteral("换算")});
    m_fields->horizontalHeader()->setStretchLastSection(true);
    m_fields->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_fields, 1);

    auto *row = new QHBoxLayout;
    m_hex = new QLineEdit(this);
    m_hex->setPlaceholderText(QStringLiteral("样例帧 hex，如 AA 55 00 64 ..."));
    auto *tryBtn = new QPushButton(QStringLiteral("试算"), this);
    row->addWidget(m_hex, 1);
    row->addWidget(tryBtn);
    root->addLayout(row);

    m_result = new QLabel(QStringLiteral("未试算"), this);
    m_result->setWordWrap(true);
    root->addWidget(m_result);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    root->addWidget(bb);

    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly))
        m_json->setPlainText(QString::fromUtf8(f.readAll()));
    else
        m_json->setPlainText(QStringLiteral("{\n  \"frame_size\": 0,\n  \"sync\": [],\n  \"fields\": []\n}"));

    connect(m_json, &QPlainTextEdit::textChanged, this, [this] { reloadTable(); });
    connect(tryBtn, &QPushButton::clicked, this, [this] { tryFrame(); });
    connect(bb, &QDialogButtonBox::accepted, this, [this] { save(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    reloadTable();
}

void ProtocolEditorDialog::reloadTable()
{
    m_fields->setRowCount(0);
    const QJsonDocument doc = QJsonDocument::fromJson(m_json->toPlainText().toUtf8());
    if (!doc.isObject())
        return;
    const QJsonArray fields = doc.object().value(QStringLiteral("fields")).toArray();
    m_fields->setRowCount(fields.size());
    for (int i = 0; i < fields.size(); ++i)
    {
        const QJsonObject o = fields.at(i).toObject();
        m_fields->setItem(i, 0, new QTableWidgetItem(o.value(QStringLiteral("name")).toString()));
        m_fields->setItem(i, 1, new QTableWidgetItem(QString::number(o.value(QStringLiteral("byte")).toInt())));
        m_fields->setItem(i, 2, new QTableWidgetItem(QString::number(o.value(QStringLiteral("bit")).toInt())));
        m_fields->setItem(i, 3, new QTableWidgetItem(QString::number(o.value(QStringLiteral("bits")).toInt(8))));
        m_fields->setItem(i, 4, new QTableWidgetItem(o.value(QStringLiteral("signed")).toBool() ? "1" : "0"));
        m_fields->setItem(i, 5, new QTableWidgetItem(
                                 QStringLiteral("×%1 +%2 %3")
                                     .arg(o.value(QStringLiteral("scale")).toDouble(1.0))
                                     .arg(o.value(QStringLiteral("offset")).toDouble(0.0))
                                     .arg(o.value(QStringLiteral("unit")).toString())));
    }
}

void ProtocolEditorDialog::tryFrame()
{
    const QJsonDocument doc = QJsonDocument::fromJson(m_json->toPlainText().toUtf8());
    if (!doc.isObject())
    {
        m_result->setText(QStringLiteral("模板 JSON 无效"));
        return;
    }
    const QJsonObject t = doc.object();
    const int frameSize = t.value(QStringLiteral("frame_size")).toInt();
    QString hex = m_hex->text();
    hex.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    const QByteArray frame = QByteArray::fromHex(hex.toLatin1());
    if (!frameSize || frame.size() < frameSize)
    {
        m_result->setText(QStringLiteral("样例帧长度不足"));
        return;
    }
    QStringList out;
    const QJsonObject crc = t.value(QStringLiteral("crc16_ccitt")).toObject();
    if (!crc.isEmpty())
    {
        const int off = crc.value(QStringLiteral("offset")).toInt(-1);
        const uint16_t want = crc16(frame, off, static_cast<uint16_t>(crc.value(QStringLiteral("poly")).toInt(0x1021)),
                                    static_cast<uint16_t>(crc.value(QStringLiteral("init")).toInt(0xFFFF)));
        uint16_t got = 0;
        if (off >= 0 && off + 2 <= frame.size())
            got = static_cast<uint16_t>(frame.at(off)) |
                  (static_cast<uint16_t>(frame.at(off + 1)) << 8);
        out << (want == got ? QStringLiteral("CRC OK") : QStringLiteral("CRC FAIL"));
    }
    else
        out << QStringLiteral("无 CRC 校验");

    for (const QJsonValue &v : t.value(QStringLiteral("fields")).toArray())
    {
        const QJsonObject f = v.toObject();
        const uint32_t byte = f.value(QStringLiteral("byte")).toInt();
        const uint32_t bit = f.value(QStringLiteral("bit")).toInt();
        const uint32_t bits = f.value(QStringLiteral("bits")).toInt(8);
        const uint64_t raw = readBits(frame, byte * 8 + bit, bits);
        double value = static_cast<double>(raw);
        if (f.value(QStringLiteral("signed")).toBool() && bits < 64 && (raw & (1ull << (bits - 1))))
            value = static_cast<double>(static_cast<int64_t>(raw | (~0ull << bits)));
        value = value * f.value(QStringLiteral("scale")).toDouble(1.0) +
                f.value(QStringLiteral("offset")).toDouble(0.0);
        out << QStringLiteral("%1=%2%3")
                   .arg(f.value(QStringLiteral("name")).toString())
                   .arg(value)
                   .arg(f.value(QStringLiteral("unit")).toString());
    }
    m_result->setText(out.join(QStringLiteral("  ")));
}

void ProtocolEditorDialog::save()
{
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, QStringLiteral("保存"), QStringLiteral("无法写入模板文件"));
        return;
    }
    f.write(m_json->toPlainText().toUtf8());
    accept();
}
