#include "ProtocolEditorDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
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
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
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

QString luaQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(out);
}

bool boolText(const QString &s)
{
    return s == QLatin1String("1") || s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QStringLiteral("协议模板编辑器"));
    resize(900, 700);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    // ---- 设计页 ----
    auto *design = new QWidget(m_tabs);
    auto *dv = new QVBoxLayout(design);
    auto *headerBox = new QGroupBox(QStringLiteral("帧结构"), design);
    auto *form = new QFormLayout(headerBox);
    m_name = new QLineEdit(headerBox);
    m_name->setPlaceholderText(QStringLiteral("例如 demo_flight"));
    m_frameSize = new QSpinBox(headerBox);
    m_frameSize->setRange(0, 4096);
    m_sync = new QLineEdit(headerBox);
    m_sync->setPlaceholderText(QStringLiteral("AA 55"));
    m_crcOffset = new QSpinBox(headerBox);
    m_crcOffset->setRange(-1, 4095);
    m_crcOffset->setSpecialValueText(QStringLiteral("无"));
    m_crcPoly = new QLineEdit(QStringLiteral("0x1021"), headerBox);
    m_crcInit = new QLineEdit(QStringLiteral("0xFFFF"), headerBox);
    form->addRow(QStringLiteral("协议名称"), m_name);
    form->addRow(QStringLiteral("帧长（字节）"), m_frameSize);
    form->addRow(QStringLiteral("同步字"), m_sync);
    form->addRow(QStringLiteral("CRC 起始字节"), m_crcOffset);
    form->addRow(QStringLiteral("CRC 多项式"), m_crcPoly);
    form->addRow(QStringLiteral("CRC 初值"), m_crcInit);
    dv->addWidget(headerBox);

    auto *fieldBar = new QHBoxLayout;
    auto *addBtn = new QPushButton(QStringLiteral("+ 字段"), design);
    auto *delBtn = new QPushButton(QStringLiteral("− 字段"), design);
    auto *hint = new QLabel(QStringLiteral("位宽 1–64；系数/偏移在界面上直接换算"), design);
    hint->setStyleSheet(QStringLiteral("color:#8b949e;"));
    fieldBar->addWidget(addBtn);
    fieldBar->addWidget(delBtn);
    fieldBar->addWidget(hint, 1);
    dv->addLayout(fieldBar);

    m_fields = new QTableWidget(0, 10, design);
    m_fields->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("字节"),
                                         QStringLiteral("起始位"), QStringLiteral("位宽"),
                                         QStringLiteral("有符号"), QStringLiteral("浮点"),
                                         QStringLiteral("系数"), QStringLiteral("偏移"),
                                         QStringLiteral("单位"), QStringLiteral("时间")});
    m_fields->horizontalHeader()->setStretchLastSection(true);
    m_fields->setAlternatingRowColors(true);
    for (int i = 0; i < 10; ++i)
        m_fields->setColumnWidth(i, i == 0 ? 160 : (i <= 3 ? 58 : (i <= 7 ? 62 : 60)));
    dv->addWidget(m_fields, 1);
    m_tabs->addTab(design, QStringLiteral("设计"));

    // ---- JSON 页 ----
    m_json = new QPlainTextEdit(m_tabs);
    m_json->setPlaceholderText(QStringLiteral("{\"frame_size\":14,\"sync\":[170,85],...}"));
    m_tabs->addTab(m_json, QStringLiteral("JSON"));

    // ---- Lua 页 ----
    m_lua = new QPlainTextEdit(m_tabs);
    m_lua->setReadOnly(true);
    m_tabs->addTab(m_lua, QStringLiteral("Lua"));

    root->addWidget(m_tabs, 1);

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
    bb->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    bb->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    auto *saveAsBtn = new QPushButton(QStringLiteral("另存为"), this);
    bb->addButton(saveAsBtn, QDialogButtonBox::ActionRole);
    root->addWidget(bb);

    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly))
        m_json->setPlainText(QString::fromUtf8(f.readAll()));
    else
        m_json->setPlainText(QStringLiteral("{\n  \"name\": \"新协议\",\n  \"frame_size\": 0,\n  \"sync\": [],\n  \"fields\": []\n}"));
    refreshDesignFromJson();

    connect(m_name, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_frameSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    connect(m_sync, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_crcOffset, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    connect(m_crcPoly, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_crcInit, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_fields, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        rebuildJsonFromDesign();
    });
    connect(addBtn, &QPushButton::clicked, this, [this] {
        m_fields->insertRow(m_fields->rowCount());
        const int r = m_fields->rowCount() - 1;
        m_fields->setItem(r, 0, new QTableWidgetItem(QStringLiteral("field%1").arg(r + 1)));
        m_fields->setItem(r, 1, new QTableWidgetItem(QString::number(std::max(0, m_frameSize->value() - 2))));
        m_fields->setItem(r, 2, new QTableWidgetItem(QStringLiteral("0")));
        m_fields->setItem(r, 3, new QTableWidgetItem(QStringLiteral("16")));
        m_fields->setItem(r, 4, new QTableWidgetItem(QStringLiteral("0")));
        m_fields->setItem(r, 5, new QTableWidgetItem(QStringLiteral("0")));
        m_fields->setItem(r, 6, new QTableWidgetItem(QStringLiteral("1")));
        m_fields->setItem(r, 7, new QTableWidgetItem(QStringLiteral("0")));
        m_fields->setItem(r, 8, new QTableWidgetItem(QString()));
        m_fields->setItem(r, 9, new QTableWidgetItem(QStringLiteral("0")));
        rebuildJsonFromDesign();
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        const int r = m_fields->currentRow();
        if (r >= 0)
        {
            m_fields->removeRow(r);
            rebuildJsonFromDesign();
        }
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (m_tabs->currentWidget() == m_json)
            m_json->setFocus();
        else
            refreshDesignFromJson();
    });
    connect(tryBtn, &QPushButton::clicked, this, [this] { tryFrame(); });
    connect(bb, &QDialogButtonBox::accepted, this, [this] { save(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveAsBtn, &QPushButton::clicked, this, [this] { saveAs(); });
    auto *saveKey = new QShortcut(QKeySequence::Save, this);
    connect(saveKey, &QShortcut::activated, this, [this] { save(); });
}

QJsonObject ProtocolEditorDialog::currentTemplate() const
{
    const QJsonDocument doc = QJsonDocument::fromJson(m_json->toPlainText().toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void ProtocolEditorDialog::refreshDesignFromJson()
{
    const QJsonObject o = currentTemplate();
    if (o.isEmpty())
        return;
    m_syncing = true;
    m_name->setText(o.value(QStringLiteral("name")).toString());
    m_frameSize->setValue(o.value(QStringLiteral("frame_size")).toInt());
    QStringList syncParts;
    for (const QJsonValue &v : o.value(QStringLiteral("sync")).toArray())
        syncParts << QStringLiteral("%1").arg(v.toInt(), 2, 16, QLatin1Char('0')).toUpper();
    m_sync->setText(syncParts.join(QLatin1Char(' ')));
    const QJsonObject crc = o.value(QStringLiteral("crc16_ccitt")).toObject();
    m_crcOffset->setValue(crc.value(QStringLiteral("offset")).toInt(-1));
    m_crcPoly->setText(QString::number(crc.value(QStringLiteral("poly")).toInt(0x1021), 16).prepend(QStringLiteral("0x")).toUpper());
    m_crcInit->setText(QString::number(crc.value(QStringLiteral("init")).toInt(0xFFFF), 16).prepend(QStringLiteral("0x")).toUpper());

    const QJsonArray fields = o.value(QStringLiteral("fields")).toArray();
    m_fields->setRowCount(fields.size());
    for (int i = 0; i < fields.size(); ++i)
    {
        const QJsonObject f = fields.at(i).toObject();
        m_fields->setItem(i, 0, new QTableWidgetItem(f.value(QStringLiteral("name")).toString()));
        m_fields->setItem(i, 1, new QTableWidgetItem(QString::number(f.value(QStringLiteral("byte")).toInt())));
        m_fields->setItem(i, 2, new QTableWidgetItem(QString::number(f.value(QStringLiteral("bit")).toInt())));
        m_fields->setItem(i, 3, new QTableWidgetItem(QString::number(f.value(QStringLiteral("bits")).toInt(8))));
        m_fields->setItem(i, 4, new QTableWidgetItem(f.value(QStringLiteral("signed")).toBool() ? QStringLiteral("1") : QStringLiteral("0")));
        m_fields->setItem(i, 5, new QTableWidgetItem(f.value(QStringLiteral("float")).toBool() ? QStringLiteral("1") : QStringLiteral("0")));
        m_fields->setItem(i, 6, new QTableWidgetItem(QString::number(f.value(QStringLiteral("scale")).toDouble(1.0))));
        m_fields->setItem(i, 7, new QTableWidgetItem(QString::number(f.value(QStringLiteral("offset")).toDouble(0.0))));
        m_fields->setItem(i, 8, new QTableWidgetItem(f.value(QStringLiteral("unit")).toString()));
        m_fields->setItem(i, 9, new QTableWidgetItem(f.value(QStringLiteral("time")).toBool() ? QStringLiteral("1") : QStringLiteral("0")));
    }
    m_syncing = false;
    rebuildLua();
    m_result->setText(QStringLiteral("已加载：%1 个字段").arg(fields.size()));
}

void ProtocolEditorDialog::rebuildJsonFromDesign()
{
    if (m_syncing)
        return;
    QJsonObject o = currentTemplate();
    if (o.isEmpty())
        o = QJsonObject();
    o.insert(QStringLiteral("name"), m_name->text().trimmed());
    o.insert(QStringLiteral("frame_size"), m_frameSize->value());

    QJsonArray sync;
    const QString syncText = m_sync->text();
    QString cleaned;
    for (const QChar &c : syncText)
        if (c.isLetterOrNumber())
            cleaned.append(c);
    for (int i = 0; i + 1 < cleaned.size(); i += 2)
        sync.append(cleaned.mid(i, 2).toInt(nullptr, 16));
    o.insert(QStringLiteral("sync"), sync);

    if (m_crcOffset->value() >= 0)
    {
        QJsonObject crc;
        bool ok = false;
        const uint poly = m_crcPoly->text().toUInt(&ok, 0);
        const uint init = m_crcInit->text().toUInt(&ok, 0);
        crc.insert(QStringLiteral("offset"), m_crcOffset->value());
        crc.insert(QStringLiteral("poly"), ok ? static_cast<int>(poly) : 0x1021);
        crc.insert(QStringLiteral("init"), ok ? static_cast<int>(init) : 0xFFFF);
        o.insert(QStringLiteral("crc16_ccitt"), crc);
    }
    else
        o.remove(QStringLiteral("crc16_ccitt"));

    QJsonArray fields;
    for (int r = 0; r < m_fields->rowCount(); ++r)
    {
        auto cell = [this, r](int c) {
            QTableWidgetItem *it = m_fields->item(r, c);
            return it ? it->text().trimmed() : QString();
        };
        QJsonObject f;
        f.insert(QStringLiteral("name"), cell(0));
        if (cell(0).isEmpty())
            continue;
        f.insert(QStringLiteral("byte"), cell(1).toInt());
        f.insert(QStringLiteral("bit"), cell(2).toInt());
        f.insert(QStringLiteral("bits"), std::clamp(cell(3).toInt(), 1, 64));
        f.insert(QStringLiteral("signed"), boolText(cell(4)));
        f.insert(QStringLiteral("float"), boolText(cell(5)));
        f.insert(QStringLiteral("scale"), cell(6).isEmpty() ? 1.0 : cell(6).toDouble());
        f.insert(QStringLiteral("offset"), cell(7).toDouble());
        f.insert(QStringLiteral("unit"), cell(8));
        f.insert(QStringLiteral("time"), boolText(cell(9)));
        fields.append(f);
    }
    o.insert(QStringLiteral("fields"), fields);

    m_syncing = true;
    m_json->setPlainText(QJsonDocument(o).toJson(QJsonDocument::Indented));
    m_syncing = false;
    rebuildLua();
}

void ProtocolEditorDialog::rebuildLua()
{
    const QJsonObject o = currentTemplate();
    if (o.isEmpty())
    {
        m_lua->setPlainText(QStringLiteral("-- JSON 无效，无法生成 Lua 描述"));
        return;
    }
    QStringList lines;
    lines << QStringLiteral("-- PlotScope 协议描述（由设计页生成）");
    lines << QStringLiteral("return {");
    lines << QStringLiteral("  name = %1,").arg(luaQuote(o.value(QStringLiteral("name")).toString()));
    lines << QStringLiteral("  frame_size = %1,").arg(o.value(QStringLiteral("frame_size")).toInt());
    QStringList sync;
    for (const QJsonValue &v : o.value(QStringLiteral("sync")).toArray())
        sync << QStringLiteral("0x%1").arg(v.toInt(), 2, 16, QLatin1Char('0')).toUpper();
    lines << QStringLiteral("  sync = {%1},").arg(sync.join(QStringLiteral(", ")));
    lines << QStringLiteral("  fields = {");
    for (const QJsonValue &v : o.value(QStringLiteral("fields")).toArray())
    {
        const QJsonObject f = v.toObject();
        lines << QStringLiteral("    { name = %1, byte = %2, bit = %3, bits = %4, signed = %5, scale = %6, offset = %7, unit = %8 },")
                     .arg(luaQuote(f.value(QStringLiteral("name")).toString()))
                     .arg(f.value(QStringLiteral("byte")).toInt())
                     .arg(f.value(QStringLiteral("bit")).toInt())
                     .arg(f.value(QStringLiteral("bits")).toInt(8))
                     .arg(f.value(QStringLiteral("signed")).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(f.value(QStringLiteral("scale")).toDouble(1.0))
                     .arg(f.value(QStringLiteral("offset")).toDouble(0.0))
                     .arg(luaQuote(f.value(QStringLiteral("unit")).toString()));
    }
    lines << QStringLiteral("  }");
    lines << QStringLiteral("}");
    m_lua->setPlainText(lines.join(QLatin1Char('\n')));
}

void ProtocolEditorDialog::tryFrame()
{
    const QJsonObject t = currentTemplate();
    if (t.isEmpty())
    {
        m_result->setText(QStringLiteral("模板 JSON 无效"));
        return;
    }
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
    if (m_tabs->currentWidget() != m_json)
        rebuildJsonFromDesign();
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, QStringLiteral("保存"), QStringLiteral("无法写入模板文件"));
        return;
    }
    f.write(m_json->toPlainText().toUtf8());
    accept();
}

void ProtocolEditorDialog::saveAs()
{
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("另存协议模板"), m_path,
        QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
    if (f.isEmpty())
        return;
    m_path = f;
    save();
}
