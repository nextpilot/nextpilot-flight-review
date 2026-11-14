#include "ProtocolEditorDialog.h"

#include <QCheckBox>
#include <QComboBox>
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

const int kFieldCols = 16;

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

QString hexByte(int v)
{
    return QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QStringLiteral("协议模板编辑器"));
    resize(940, 760);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    auto *design = new QWidget(m_tabs);
    auto *dv = new QVBoxLayout(design);
    auto *headerBox = new QGroupBox(QStringLiteral("帧结构"), design);
    auto *form = new QFormLayout(headerBox);
    m_name = new QLineEdit(headerBox);
    m_name->setPlaceholderText(QStringLiteral("例如 demo_flight"));
    m_frameKind = new QComboBox(headerBox);
    m_frameKind->addItems({QStringLiteral("固定帧长"), QStringLiteral("变长（长度字段）")});
    m_frameSize = new QSpinBox(headerBox);
    m_frameSize->setRange(0, 4096);
    m_lengthByte = new QSpinBox(headerBox);
    m_lengthByte->setRange(0, 4095);
    m_lengthBytes = new QSpinBox(headerBox);
    m_lengthBytes->setRange(1, 4);
    m_lengthEndian = new QComboBox(headerBox);
    m_lengthEndian->addItems({QStringLiteral("little"), QStringLiteral("big")});
    m_lengthHeader = new QCheckBox(QStringLiteral("长度包含帧头"), headerBox);
    m_lengthHeader->setChecked(true);
    m_sync = new QLineEdit(QStringLiteral("0:AA 1:55"), headerBox);
    m_sync->setPlaceholderText(QStringLiteral("偏移:值，如 0:AA 1:55；也可只写 AA 55"));
    m_crcOffset = new QSpinBox(headerBox);
    m_crcOffset->setRange(-1, 4095);
    m_crcOffset->setSpecialValueText(QStringLiteral("无"));
    m_crcPoly = new QLineEdit(QStringLiteral("0x1021"), headerBox);
    m_crcInit = new QLineEdit(QStringLiteral("0xFFFF"), headerBox);
    form->addRow(QStringLiteral("协议名称"), m_name);
    form->addRow(QStringLiteral("帧类型"), m_frameKind);
    form->addRow(QStringLiteral("固定帧长（字节）"), m_frameSize);
    form->addRow(QStringLiteral("长度字段字节偏移"), m_lengthByte);
    form->addRow(QStringLiteral("长度字段字节数"), m_lengthBytes);
    form->addRow(QStringLiteral("长度字节序"), m_lengthEndian);
    form->addRow(QString(), m_lengthHeader);
    form->addRow(QStringLiteral("同步字"), m_sync);
    form->addRow(QStringLiteral("全局 CRC 起始字节"), m_crcOffset);
    form->addRow(QStringLiteral("CRC 多项式"), m_crcPoly);
    form->addRow(QStringLiteral("CRC 初值"), m_crcInit);
    dv->addWidget(headerBox);

    auto *fieldBar = new QHBoxLayout;
    auto *addBtn = new QPushButton(QStringLiteral("+ 字段"), design);
    auto *delBtn = new QPushButton(QStringLiteral("− 字段"), design);
    auto *hint = new QLabel(QStringLiteral("常量匹配原始位值；CRC 起止字节含端点"), design);
    hint->setStyleSheet(QStringLiteral("color:#8b949e;"));
    fieldBar->addWidget(addBtn);
    fieldBar->addWidget(delBtn);
    fieldBar->addWidget(hint, 1);
    dv->addLayout(fieldBar);

    m_fields = new QTableWidget(0, kFieldCols, design);
    setupFieldTable(m_fields);
    dv->addWidget(m_fields, 1);
    m_tabs->addTab(design, QStringLiteral("设计"));

    auto *series = new QWidget(m_tabs);
    auto *sv = new QVBoxLayout(series);
    auto *sTop = new QHBoxLayout;
    m_series = new QCheckBox(QStringLiteral("启用系列协议（同一帧结构，按 msgid 解析 payload）"), series);
    m_msgIdByte = new QSpinBox(series);
    m_msgIdByte->setRange(0, 4095);
    m_msgIdByte->setValue(3);
    m_msgIdBits = new QSpinBox(series);
    m_msgIdBits->setRange(1, 64);
    m_msgIdBits->setValue(8);
    sTop->addWidget(m_series, 1);
    sTop->addWidget(new QLabel(QStringLiteral("msgid 字节"), series));
    sTop->addWidget(m_msgIdByte);
    sTop->addWidget(new QLabel(QStringLiteral("位宽"), series));
    sTop->addWidget(m_msgIdBits);
    sv->addLayout(sTop);

    auto *varBar = new QHBoxLayout;
    auto *addVar = new QPushButton(QStringLiteral("+ 变体"), series);
    auto *delVar = new QPushButton(QStringLiteral("− 变体"), series);
    m_variants = new QTableWidget(0, 2, series);
    m_variants->setHorizontalHeaderLabels({QStringLiteral("msgid"), QStringLiteral("变体名")});
    m_variants->setMaximumWidth(300);
    m_variants->setMaximumHeight(140);
    varBar->addWidget(addVar);
    varBar->addWidget(delVar);
    varBar->addStretch();
    sv->addLayout(varBar);
    sv->addWidget(m_variants, 0);

    auto *varFieldBar = new QHBoxLayout;
    auto *addVarField = new QPushButton(QStringLiteral("+ 变体字段"), series);
    varFieldBar->addWidget(addVarField);
    varFieldBar->addStretch();
    sv->addLayout(varFieldBar);
    m_varFields = new QTableWidget(0, kFieldCols, series);
    setupFieldTable(m_varFields);
    sv->addWidget(m_varFields, 1);
    m_tabs->addTab(series, QStringLiteral("系列"));

    m_json = new QPlainTextEdit(m_tabs);
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabs->addTab(m_json, QStringLiteral("JSON"));
    m_lua = new QPlainTextEdit(m_tabs);
    m_lua->setReadOnly(true);
    m_tabs->addTab(m_lua, QStringLiteral("Lua"));

    root->addWidget(m_tabs, 1);

    auto *row = new QHBoxLayout;
    m_hex = new QLineEdit(this);
    m_hex->setPlaceholderText(QStringLiteral("样例帧 hex，如 AA 55 0C 00 01 ..."));
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
    connect(m_frameKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const bool var = m_frameKind->currentIndex() == 1;
        m_frameSize->setEnabled(!var);
        m_lengthByte->setEnabled(var);
        m_lengthBytes->setEnabled(var);
        m_lengthEndian->setEnabled(var);
        m_lengthHeader->setEnabled(var);
        rebuildJsonFromDesign();
    });
    connect(m_frameSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    for (QSpinBox *sp : {m_lengthByte, m_lengthBytes})
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { rebuildJsonFromDesign(); });
    connect(m_lengthEndian, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    connect(m_lengthHeader, &QCheckBox::toggled, this, [this](bool) { rebuildJsonFromDesign(); });
    connect(m_sync, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_crcOffset, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    connect(m_crcPoly, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_crcInit, &QLineEdit::textEdited, this, [this] { rebuildJsonFromDesign(); });
    connect(m_fields, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        rebuildJsonFromDesign();
    });
    connect(addBtn, &QPushButton::clicked, this, [this] { addCommonField(); });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        const int r = m_fields->currentRow();
        if (r >= 0)
        {
            m_fields->removeRow(r);
            rebuildJsonFromDesign();
        }
    });

    connect(m_series, &QCheckBox::toggled, this, [this](bool on) {
        m_msgIdByte->setEnabled(on);
        m_msgIdBits->setEnabled(on);
        m_variants->setEnabled(on);
        rebuildJsonFromDesign();
    });
    connect(m_msgIdByte, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    connect(m_msgIdBits, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { rebuildJsonFromDesign(); });
    connect(m_variants, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_syncing || m_currentVariant < 0 || m_currentVariant >= m_variantData.size())
            return;
        QJsonObject v = m_variantData.at(m_currentVariant).toObject();
        if (it->column() == 0)
            v.insert(QStringLiteral("id"), it->text().toInt());
        else
            v.insert(QStringLiteral("name"), it->text());
        m_variantData.replace(m_currentVariant, v);
        rebuildJsonFromDesign();
    });
    connect(m_variants, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (m_syncing)
            return;
        storeVariantFields();
        loadVariantRow(row);
    });
    connect(m_varFields, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        if (m_syncing)
            return;
        storeVariantFields();
        rebuildJsonFromDesign();
    });
    connect(addVar, &QPushButton::clicked, this, [this] {
        storeVariantFields();
        const int id = m_variants->rowCount() + 1;
        QJsonObject v;
        v.insert(QStringLiteral("id"), id);
        v.insert(QStringLiteral("name"), QStringLiteral("var%1").arg(id));
        v.insert(QStringLiteral("fields"), QJsonArray());
        m_variantData.append(v);
        m_variants->insertRow(m_variants->rowCount());
        m_variants->setItem(m_variants->rowCount() - 1, 0, new QTableWidgetItem(QString::number(id)));
        m_variants->setItem(m_variants->rowCount() - 1, 1, new QTableWidgetItem(QStringLiteral("var%1").arg(id)));
        m_variants->setCurrentCell(m_variants->rowCount() - 1, 0);
        loadVariantRow(m_variants->rowCount() - 1);
        rebuildJsonFromDesign();
    });
    connect(delVar, &QPushButton::clicked, this, [this] {
        const int r = m_variants->currentRow();
        if (r < 0)
            return;
        m_variantData.removeAt(r);
        m_variants->removeRow(r);
        m_currentVariant = -1;
        loadVariantRow(std::min(r, m_variants->rowCount() - 1));
        rebuildJsonFromDesign();
    });
    connect(addVarField, &QPushButton::clicked, this, [this] { addVariantField(); });

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (m_tabs->currentWidget() != m_json)
            refreshDesignFromJson();
    });
    connect(tryBtn, &QPushButton::clicked, this, [this] { tryFrame(); });
    connect(bb, &QDialogButtonBox::accepted, this, [this] { save(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveAsBtn, &QPushButton::clicked, this, [this] { saveAs(); });
    auto *saveKey = new QShortcut(QKeySequence::Save, this);
    connect(saveKey, &QShortcut::activated, this, [this] { save(); });

    const bool var = m_frameKind->currentIndex() == 1;
    m_frameSize->setEnabled(!var);
    m_lengthByte->setEnabled(var);
    m_lengthBytes->setEnabled(var);
    m_lengthEndian->setEnabled(var);
    m_lengthHeader->setEnabled(var);
}

void ProtocolEditorDialog::setupFieldTable(QTableWidget *t)
{
    t->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("字节"),
                                  QStringLiteral("位"), QStringLiteral("位宽"),
                                  QStringLiteral("有符号"), QStringLiteral("浮点"),
                                  QStringLiteral("系数"), QStringLiteral("偏移"),
                                  QStringLiteral("单位"), QStringLiteral("时间"),
                                  QStringLiteral("常量"), QStringLiteral("最小"),
                                  QStringLiteral("最大"), QStringLiteral("CRC起"),
                                  QStringLiteral("CRC止"), QStringLiteral("隐藏")});
    t->horizontalHeader()->setStretchLastSection(true);
    t->setAlternatingRowColors(true);
    const int widths[kFieldCols] = {120, 46, 40, 44, 48, 40, 46, 46, 46, 40, 52, 46, 46, 48, 48, 44};
    for (int i = 0; i < kFieldCols; ++i)
        t->setColumnWidth(i, widths[i]);
}

void ProtocolEditorDialog::populateFields(QTableWidget *t, const QJsonArray &fields)
{
    t->setRowCount(fields.size());
    for (int i = 0; i < fields.size(); ++i)
    {
        const QJsonObject f = fields.at(i).toObject();
        const QJsonObject chk = f.value(QStringLiteral("check")).toObject();
        auto put = [t, i](int c, const QString &v) { t->setItem(i, c, new QTableWidgetItem(v)); };
        put(0, f.value(QStringLiteral("name")).toString());
        put(1, QString::number(f.value(QStringLiteral("byte")).toInt()));
        put(2, QString::number(f.value(QStringLiteral("bit")).toInt()));
        put(3, QString::number(f.value(QStringLiteral("bits")).toInt(8)));
        put(4, f.value(QStringLiteral("signed")).toBool() ? QStringLiteral("1") : QStringLiteral("0"));
        put(5, f.value(QStringLiteral("float")).toBool() ? QStringLiteral("1") : QStringLiteral("0"));
        put(6, QString::number(f.value(QStringLiteral("scale")).toDouble(1.0)));
        put(7, QString::number(f.value(QStringLiteral("offset")).toDouble(0.0)));
        put(8, f.value(QStringLiteral("unit")).toString());
        put(9, f.value(QStringLiteral("time")).toBool() ? QStringLiteral("1") : QStringLiteral("0"));
        put(10, f.contains(QStringLiteral("equals")) ? QString::number(f.value(QStringLiteral("equals")).toDouble()) : QString());
        put(11, f.contains(QStringLiteral("min")) ? QString::number(f.value(QStringLiteral("min")).toDouble()) : QString());
        put(12, f.contains(QStringLiteral("max")) ? QString::number(f.value(QStringLiteral("max")).toDouble()) : QString());
        put(13, chk.isEmpty() ? QString() : QString::number(chk.value(QStringLiteral("from")).toInt()));
        put(14, chk.isEmpty() ? QString() : QString::number(chk.value(QStringLiteral("to")).toInt()));
        put(15, f.value(QStringLiteral("hidden")).toBool() ? QStringLiteral("1") : QStringLiteral("0"));
    }
}

QJsonArray ProtocolEditorDialog::fieldsFromTable(QTableWidget *t) const
{
    QJsonArray fields;
    for (int r = 0; r < t->rowCount(); ++r)
    {
        auto cell = [t, r](int c) {
            QTableWidgetItem *it = t->item(r, c);
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
        if (!cell(10).isEmpty())
            f.insert(QStringLiteral("equals"), cell(10).toDouble());
        if (!cell(11).isEmpty())
            f.insert(QStringLiteral("min"), cell(11).toDouble());
        if (!cell(12).isEmpty())
            f.insert(QStringLiteral("max"), cell(12).toDouble());
        if (boolText(cell(15)))
            f.insert(QStringLiteral("hidden"), true);
        if (!cell(13).isEmpty() && !cell(14).isEmpty())
        {
            QJsonObject chk;
            chk.insert(QStringLiteral("type"), QStringLiteral("crc16"));
            chk.insert(QStringLiteral("from"), cell(13).toInt());
            chk.insert(QStringLiteral("to"), cell(14).toInt());
            f.insert(QStringLiteral("check"), chk);
        }
        fields.append(f);
    }
    return fields;
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
    const QJsonObject lj = o.value(QStringLiteral("length")).toObject();
    m_frameKind->setCurrentIndex(lj.isEmpty() ? 0 : 1);
    m_frameSize->setValue(o.value(QStringLiteral("frame_size")).toInt());
    m_lengthByte->setValue(lj.value(QStringLiteral("byte")).toInt(2));
    m_lengthBytes->setValue(lj.value(QStringLiteral("bytes")).toInt(2));
    m_lengthEndian->setCurrentText(lj.value(QStringLiteral("endian")).toString(QLatin1String("little")));
    m_lengthHeader->setChecked(lj.value(QStringLiteral("includes_header")).toBool(true));

    QStringList syncParts;
    int seq = 0;
    for (const QJsonValue &v : o.value(QStringLiteral("sync")).toArray())
    {
        if (v.isObject())
        {
            const QJsonObject so = v.toObject();
            syncParts << QStringLiteral("%1:%2").arg(so.value(QStringLiteral("offset")).toInt(seq)).arg(hexByte(so.value(QStringLiteral("value")).toInt()));
        }
        else
            syncParts << QStringLiteral("%1:%2").arg(seq).arg(hexByte(v.toInt()));
        ++seq;
    }
    m_sync->setText(syncParts.join(QLatin1Char(' ')));
    const QJsonObject crc = o.value(QStringLiteral("crc16_ccitt")).toObject();
    m_crcOffset->setValue(crc.value(QStringLiteral("offset")).toInt(-1));
    m_crcPoly->setText(QStringLiteral("0x%1").arg(crc.value(QStringLiteral("poly")).toInt(0x1021), 4, 16, QLatin1Char('0')).toUpper());
    m_crcInit->setText(QStringLiteral("0x%1").arg(crc.value(QStringLiteral("init")).toInt(0xFFFF), 4, 16, QLatin1Char('0')).toUpper());

    populateFields(m_fields, o.value(QStringLiteral("fields")).toArray());

    const QJsonObject mid = o.value(QStringLiteral("msgid")).toObject();
    m_series->setChecked(!mid.isEmpty());
    m_msgIdByte->setValue(mid.value(QStringLiteral("byte")).toInt(3));
    m_msgIdBits->setValue(mid.value(QStringLiteral("bits")).toInt(8));

    m_variantData = o.value(QStringLiteral("variants")).toArray();
    m_currentVariant = -1;
    m_variants->setRowCount(m_variantData.size());
    for (int i = 0; i < m_variantData.size(); ++i)
    {
        const QJsonObject v = m_variantData.at(i).toObject();
        m_variants->setItem(i, 0, new QTableWidgetItem(QString::number(v.value(QStringLiteral("id")).toInt())));
        m_variants->setItem(i, 1, new QTableWidgetItem(v.value(QStringLiteral("name")).toString()));
    }
    m_syncing = false;
    if (m_variants->rowCount() > 0)
        loadVariantRow(0);
    else
        m_varFields->setRowCount(0);
    rebuildLua();
    m_result->setText(QStringLiteral("已加载：%1 个字段，%2 个变体")
                          .arg(m_fields->rowCount()).arg(m_variants->rowCount()));
}

void ProtocolEditorDialog::storeVariantFields()
{
    if (m_currentVariant < 0 || m_currentVariant >= m_variantData.size())
        return;
    QJsonObject v = m_variantData.at(m_currentVariant).toObject();
    v.insert(QStringLiteral("fields"), fieldsFromTable(m_varFields));
    m_variantData.replace(m_currentVariant, v);
}

void ProtocolEditorDialog::loadVariantRow(int row)
{
    storeVariantFields();
    if (row < 0 || row >= m_variantData.size())
    {
        m_currentVariant = -1;
        m_varFields->setRowCount(0);
        return;
    }
    m_currentVariant = row;
    m_syncing = true;
    populateFields(m_varFields, m_variantData.at(row).toObject().value(QStringLiteral("fields")).toArray());
    m_syncing = false;
}

void ProtocolEditorDialog::addCommonField()
{
    m_fields->insertRow(m_fields->rowCount());
    const int r = m_fields->rowCount() - 1;
    for (int c = 0; c < kFieldCols; ++c)
        m_fields->setItem(r, c, new QTableWidgetItem(QString()));
    m_fields->item(r, 0)->setText(QStringLiteral("field%1").arg(r + 1));
    m_fields->item(r, 1)->setText(QString::number(std::max(0, m_frameSize->value() - 2)));
    m_fields->item(r, 3)->setText(QStringLiteral("16"));
    m_fields->item(r, 6)->setText(QStringLiteral("1"));
    rebuildJsonFromDesign();
}

void ProtocolEditorDialog::addVariantField()
{
    if (m_currentVariant < 0)
        return;
    m_varFields->insertRow(m_varFields->rowCount());
    const int r = m_varFields->rowCount() - 1;
    for (int c = 0; c < kFieldCols; ++c)
        m_varFields->setItem(r, c, new QTableWidgetItem(QString()));
    m_varFields->item(r, 0)->setText(QStringLiteral("field%1").arg(r + 1));
    m_varFields->item(r, 3)->setText(QStringLiteral("16"));
    m_varFields->item(r, 6)->setText(QStringLiteral("1"));
    storeVariantFields();
    rebuildJsonFromDesign();
}

void ProtocolEditorDialog::rebuildJsonFromDesign()
{
    if (m_syncing)
        return;
    QJsonObject o = currentTemplate();

    o.insert(QStringLiteral("name"), m_name->text().trimmed());
    if (m_frameKind->currentIndex() == 1)
    {
        QJsonObject lj;
        lj.insert(QStringLiteral("byte"), m_lengthByte->value());
        lj.insert(QStringLiteral("bytes"), m_lengthBytes->value());
        lj.insert(QStringLiteral("endian"), m_lengthEndian->currentText());
        lj.insert(QStringLiteral("includes_header"), m_lengthHeader->isChecked());
        o.insert(QStringLiteral("length"), lj);
        o.insert(QStringLiteral("frame_size"), 4096);
    }
    else
    {
        o.remove(QStringLiteral("length"));
        o.insert(QStringLiteral("frame_size"), m_frameSize->value());
    }

    QJsonArray sync;
    const QString syncText = m_sync->text();
    QStringList tokens = syncText.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    int seq = 0;
    for (const QString &tok : tokens)
    {
        QJsonObject sb;
        if (tok.contains(QLatin1Char(':')))
        {
            const QStringList kv = tok.split(QLatin1Char(':'));
            sb.insert(QStringLiteral("offset"), kv.value(0).toInt());
            sb.insert(QStringLiteral("value"), kv.value(1).toInt(nullptr, 16));
            seq = kv.value(0).toInt() + 1;
        }
        else
        {
            sb.insert(QStringLiteral("offset"), seq);
            sb.insert(QStringLiteral("value"), tok.toInt(nullptr, 16));
            ++seq;
        }
        sync.append(sb);
    }
    o.insert(QStringLiteral("sync"), sync);

    if (m_crcOffset->value() >= 0)
    {
        QJsonObject crc;
        bool ok = false;
        const uint poly = m_crcPoly->text().toUInt(&ok, 0);
        const bool polyOk = ok;
        const uint init = m_crcInit->text().toUInt(&ok, 0);
        crc.insert(QStringLiteral("offset"), m_crcOffset->value());
        crc.insert(QStringLiteral("poly"), polyOk ? static_cast<int>(poly) : 0x1021);
        crc.insert(QStringLiteral("init"), ok ? static_cast<int>(init) : 0xFFFF);
        o.insert(QStringLiteral("crc16_ccitt"), crc);
    }
    else
        o.remove(QStringLiteral("crc16_ccitt"));

    o.insert(QStringLiteral("fields"), fieldsFromTable(m_fields));

    if (m_series->isChecked())
    {
        QJsonObject mid;
        mid.insert(QStringLiteral("byte"), m_msgIdByte->value());
        mid.insert(QStringLiteral("bits"), m_msgIdBits->value());
        o.insert(QStringLiteral("msgid"), mid);
        storeVariantFields();
        o.insert(QStringLiteral("variants"), m_variantData);
    }
    else
    {
        o.remove(QStringLiteral("msgid"));
        o.remove(QStringLiteral("variants"));
    }

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
    auto fieldLines = [](const QJsonArray &fields) {
        QStringList out;
        for (const QJsonValue &v : fields)
        {
            const QJsonObject f = v.toObject();
            QString line = QStringLiteral("    { name = %1, byte = %2, bit = %3, bits = %4, signed = %5, scale = %6, offset = %7, unit = %8")
                               .arg(luaQuote(f.value(QStringLiteral("name")).toString()))
                               .arg(f.value(QStringLiteral("byte")).toInt())
                               .arg(f.value(QStringLiteral("bit")).toInt())
                               .arg(f.value(QStringLiteral("bits")).toInt(8))
                               .arg(f.value(QStringLiteral("signed")).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(f.value(QStringLiteral("scale")).toDouble(1.0))
                               .arg(f.value(QStringLiteral("offset")).toDouble(0.0))
                               .arg(luaQuote(f.value(QStringLiteral("unit")).toString()));
            if (f.contains(QStringLiteral("equals")))
                line += QStringLiteral(", equals = %1").arg(f.value(QStringLiteral("equals")).toDouble());
            if (f.contains(QStringLiteral("min")))
                line += QStringLiteral(", min = %1").arg(f.value(QStringLiteral("min")).toDouble());
            if (f.contains(QStringLiteral("max")))
                line += QStringLiteral(", max = %1").arg(f.value(QStringLiteral("max")).toDouble());
            line += QStringLiteral(" },");
            out << line;
        }
        return out;
    };

    QStringList lines;
    lines << QStringLiteral("-- PlotScope 协议描述（由设计页生成）");
    lines << QStringLiteral("return {");
    lines << QStringLiteral("  name = %1,").arg(luaQuote(o.value(QStringLiteral("name")).toString()));
    lines << QStringLiteral("  frame_size = %1,").arg(o.value(QStringLiteral("frame_size")).toInt());
    QStringList sync;
    for (const QJsonValue &v : o.value(QStringLiteral("sync")).toArray())
    {
        const QJsonObject so = v.toObject();
        sync << QStringLiteral("{ offset = %1, value = 0x%2 }")
                    .arg(so.value(QStringLiteral("offset")).toInt())
                    .arg(hexByte(so.value(QStringLiteral("value")).toInt()));
    }
    lines << QStringLiteral("  sync = {%1},").arg(sync.join(QStringLiteral(", ")));
    if (!o.value(QStringLiteral("length")).toObject().isEmpty())
    {
        const QJsonObject lj = o.value(QStringLiteral("length")).toObject();
        lines << QStringLiteral("  length = { byte = %1, bytes = %2, endian = %3, includes_header = %4 },")
                     .arg(lj.value(QStringLiteral("byte")).toInt())
                     .arg(lj.value(QStringLiteral("bytes")).toInt(2))
                     .arg(luaQuote(lj.value(QStringLiteral("endian")).toString()))
                     .arg(lj.value(QStringLiteral("includes_header")).toBool(true) ? QStringLiteral("true") : QStringLiteral("false"));
    }
    lines << QStringLiteral("  fields = {");
    lines += fieldLines(o.value(QStringLiteral("fields")).toArray());
    lines << QStringLiteral("  }");
    if (!o.value(QStringLiteral("msgid")).toObject().isEmpty())
    {
        const QJsonObject mid = o.value(QStringLiteral("msgid")).toObject();
        lines << QStringLiteral("  msgid = { byte = %1, bits = %2 },")
                     .arg(mid.value(QStringLiteral("byte")).toInt())
                     .arg(mid.value(QStringLiteral("bits")).toInt(8));
        lines << QStringLiteral("  variants = {");
        for (const QJsonValue &vv : o.value(QStringLiteral("variants")).toArray())
        {
            const QJsonObject var = vv.toObject();
            lines << QStringLiteral("    { id = %1, name = %2, fields = {")
                         .arg(var.value(QStringLiteral("id")).toInt())
                         .arg(luaQuote(var.value(QStringLiteral("name")).toString()));
            lines += fieldLines(var.value(QStringLiteral("fields")).toArray());
            lines << QStringLiteral("    } },");
        }
        lines << QStringLiteral("  }");
    }
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
    QString hex = m_hex->text();
    hex.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    const QByteArray frame = QByteArray::fromHex(hex.toLatin1());
    if (frame.isEmpty())
    {
        m_result->setText(QStringLiteral("填入样例帧 hex"));
        return;
    }
    QStringList out;
    for (const QJsonValue &vv : t.value(QStringLiteral("sync")).toArray())
    {
        const QJsonObject so = vv.toObject();
        const int off = so.value(QStringLiteral("offset")).toInt();
        const int want = so.value(QStringLiteral("value")).toInt();
        if (off >= frame.size() || frame.at(off) != want)
        {
            m_result->setText(QStringLiteral("同步字不匹配"));
            return;
        }
    }
    const int frameSize = t.value(QStringLiteral("frame_size")).toInt();
    if (frameSize && frame.size() < frameSize)
    {
        m_result->setText(QStringLiteral("样例帧长度不足"));
        return;
    }
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
    QJsonArray fields = t.value(QStringLiteral("fields")).toArray();
    const QJsonObject mid = t.value(QStringLiteral("msgid")).toObject();
    if (!mid.isEmpty())
    {
        const uint32_t byte = mid.value(QStringLiteral("byte")).toInt();
        const uint32_t bits = mid.value(QStringLiteral("bits")).toInt(8);
        const uint64_t id = readBits(frame, byte * 8, bits);
        out << QStringLiteral("msgid=%1").arg(id);
        for (const QJsonValue &vv : t.value(QStringLiteral("variants")).toArray())
        {
            const QJsonObject var = vv.toObject();
            if (var.value(QStringLiteral("id")).toInt() == static_cast<int>(id))
            {
                for (const QJsonValue &fv : var.value(QStringLiteral("fields")).toArray())
                    fields.append(fv);
                break;
            }
        }
    }
    for (const QJsonValue &v : fields)
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
