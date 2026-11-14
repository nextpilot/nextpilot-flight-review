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
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSet>
#include <QMap>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDropEvent>

#include <algorithm>
#include <cstdint>
#include <functional>

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

namespace {
class ReorderTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    std::function<void(int, int)> onMove;

protected:
    void dropEvent(QDropEvent *e) override
    {
        int from = currentRow();
        if (from < 0 && !selectedItems().isEmpty())
            from = selectedItems().first()->row();
        int row = indexAt(e->position().toPoint()).row();
        if (row < 0)
            row = rowCount();
        if (dropIndicatorPosition() == QAbstractItemView::BelowItem)
            ++row;
        if (from < row)
            --row;
        row = std::max(0, std::min(row, rowCount() - 1));
        if (from >= 0 && from != row && onMove)
        {
            onMove(from, row);
            e->acceptProposedAction();
            return;
        }
        QTableWidget::dropEvent(e);
    }
};

void moveTableRow(QTableWidget *t, int from, int to)
{
    if (!t || from == to || from < 0 || from >= t->rowCount() || to < 0 || to >= t->rowCount())
        return;
    QList<QTableWidgetItem *> items;
    for (int c = 0; c < t->columnCount(); ++c)
        items.append(t->takeItem(from, c));
    t->removeRow(from);
    t->insertRow(to);
    for (int c = 0; c < t->columnCount(); ++c)
        t->setItem(to, c, items.value(c));
    t->setCurrentCell(to, 0);
}
} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QStringLiteral("协议编辑器"));
    resize(1060, 900);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    /* ---- 顺序段协议页：字段 / 长度 / 类型 / 取值校验 ---- */
    m_seqPage = new QWidget(m_tabs);
    auto *seqLayout = new QVBoxLayout(m_seqPage);
    auto *seqHead = new QGroupBox(QStringLiteral("协议"), m_seqPage);
    auto *seqForm = new QFormLayout(seqHead);
    m_seqName = new QLineEdit(seqHead);
    m_seqDesc = new QLineEdit(seqHead);
    m_seqMaxsize = new QSpinBox(seqHead);
    m_seqMaxsize->setRange(2, 65535);
    m_seqMaxsize->setValue(4096);
    m_seqEndian = new QComboBox(seqHead);
    m_seqEndian->addItems({QStringLiteral("little"), QStringLiteral("big")});
    m_seqTimestamp = new QLineEdit(seqHead);
    m_seqTimestamp->setPlaceholderText(QStringLiteral("如 day*24*3600 + sec + ms/1000"));
    m_seqTsUnit = new QComboBox(seqHead);
    m_seqTsUnit->addItems({QStringLiteral("s"), QStringLiteral("ms"), QStringLiteral("us")});
    auto *tsRow = new QHBoxLayout;
    tsRow->addWidget(m_seqTimestamp, 1);
    tsRow->addWidget(new QLabel(QStringLiteral("单位"), seqHead));
    tsRow->addWidget(m_seqTsUnit);
    seqForm->addRow(QStringLiteral("名称"), m_seqName);
    seqForm->addRow(QStringLiteral("描述"), m_seqDesc);
    seqForm->addRow(QStringLiteral("最大帧长"), m_seqMaxsize);
    seqForm->addRow(QStringLiteral("字节序"), m_seqEndian);
    seqForm->addRow(QStringLiteral("时间戳表达式"), tsRow);
    seqLayout->addWidget(seqHead);

    auto *seqBar = new QHBoxLayout;
    auto *seqAdd = new QPushButton(QStringLiteral("+ 段"), m_seqPage);
    auto *seqDel = new QPushButton(QStringLiteral("− 段"), m_seqPage);
    auto *seqHint = new QLabel(QStringLiteral("长度=元素个数，类型=元素类型；校验省略算子即等于，多条用 ; 分隔；拖拽行调整顺序"), m_seqPage);
    seqHint->setStyleSheet(QStringLiteral("color:#8b949e;"));
    seqBar->addWidget(seqAdd);
    seqBar->addWidget(seqDel);
    seqBar->addWidget(seqHint, 1);
    seqLayout->addLayout(seqBar);

    m_seqFields = new ReorderTable(0, 4, m_seqPage);
    m_seqFields->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("长度"),
                                            QStringLiteral("类型"), QStringLiteral("取值 / 校验")});
    m_seqFields->horizontalHeader()->setStretchLastSection(true);
    m_seqFields->setColumnWidth(0, 150);
    m_seqFields->setColumnWidth(1, 110);
    m_seqFields->setColumnWidth(2, 100);
    m_seqFields->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_seqFields->setSelectionMode(QAbstractItemView::SingleSelection);
    m_seqFields->setDragEnabled(true);
    m_seqFields->setAcceptDrops(true);
    m_seqFields->viewport()->setAcceptDrops(true);
    m_seqFields->setDropIndicatorShown(true);
    m_seqFields->setDragDropMode(QAbstractItemView::InternalMove);
    m_seqFields->setDefaultDropAction(Qt::MoveAction);
    static_cast<ReorderTable *>(m_seqFields)->onMove = [this](int from, int to) {
        moveTableRow(m_seqFields, from, to);
        if (!m_syncing)
            rebuildSeqJson();
    };
    seqLayout->addWidget(m_seqFields, 2);

    auto *seriesBox = new QGroupBox(QStringLiteral("系列协议"), m_seqPage);
    auto *seriesLayout = new QVBoxLayout(seriesBox);
    auto *seriesTop = new QHBoxLayout;
    m_seqSeries = new QCheckBox(QStringLiteral("启用"), seriesBox);
    m_seqSelector = new QLineEdit(seriesBox);
    m_seqSelector->setPlaceholderText(QStringLiteral("selector，如 msgid & 0x7F"));
    m_seqPayload = new QLineEdit(seriesBox);
    m_seqPayload->setPlaceholderText(QStringLiteral("payload 段名"));
    m_seqFallback = new QComboBox(seriesBox);
    m_seqFallback->addItems({QStringLiteral("reject"), QStringLiteral("raw")});
    seriesTop->addWidget(m_seqSeries);
    seriesTop->addWidget(new QLabel(QStringLiteral("selector"), seriesBox));
    seriesTop->addWidget(m_seqSelector, 1);
    seriesTop->addWidget(new QLabel(QStringLiteral("payload"), seriesBox));
    seriesTop->addWidget(m_seqPayload);
    seriesTop->addWidget(new QLabel(QStringLiteral("fallback"), seriesBox));
    seriesTop->addWidget(m_seqFallback);
    seriesLayout->addLayout(seriesTop);

    auto *varTop = new QHBoxLayout;
    auto *varAdd = new QPushButton(QStringLiteral("+ 变体"), seriesBox);
    auto *varDel = new QPushButton(QStringLiteral("− 变体"), seriesBox);
    varAdd->setMaximumWidth(84);
    varDel->setMaximumWidth(84);
    m_seqVariants = new QTableWidget(0, 2, seriesBox);
    m_seqVariants->setHorizontalHeaderLabels({QStringLiteral("取值"), QStringLiteral("变体名")});
    m_seqVariants->setMinimumWidth(230);
    m_seqVariants->setMaximumWidth(260);
    m_seqVariants->setMinimumHeight(110);
    m_seqVariants->setMaximumHeight(110);
    varTop->addWidget(varAdd);
    varTop->addWidget(varDel);
    varTop->addWidget(m_seqVariants);
    varTop->addWidget(new QLabel(QStringLiteral("变体段"), seriesBox));
    varTop->addStretch(1);
    seriesLayout->addLayout(varTop);

    m_seqVarFields = new ReorderTable(0, 4, seriesBox);
    m_seqVarFields->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("长度"),
                                               QStringLiteral("类型"), QStringLiteral("取值 / 校验")});
    m_seqVarFields->horizontalHeader()->setStretchLastSection(true);
    m_seqVarFields->setColumnWidth(0, 150);
    m_seqVarFields->setColumnWidth(1, 110);
    m_seqVarFields->setColumnWidth(2, 100);
    m_seqVarFields->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_seqVarFields->setSelectionMode(QAbstractItemView::SingleSelection);
    m_seqVarFields->setDragEnabled(true);
    m_seqVarFields->setAcceptDrops(true);
    m_seqVarFields->viewport()->setAcceptDrops(true);
    m_seqVarFields->setDropIndicatorShown(true);
    m_seqVarFields->setDragDropMode(QAbstractItemView::InternalMove);
    m_seqVarFields->setDefaultDropAction(Qt::MoveAction);
    static_cast<ReorderTable *>(m_seqVarFields)->onMove = [this](int from, int to) {
        moveTableRow(m_seqVarFields, from, to);
        if (!m_syncing)
        {
            storeSeqVariantFields();
            rebuildSeqJson();
        }
    };
    seriesLayout->addWidget(m_seqVarFields, 1);
    seqLayout->addWidget(seriesBox, 1);

    m_seqPreview = new QLabel(m_seqPage);
    m_seqPreview->setWordWrap(true);
    m_seqPreview->setStyleSheet(QStringLiteral("color:#8b949e;"));
    seqLayout->addWidget(m_seqPreview);
    m_tabs->addTab(m_seqPage, QStringLiteral("协议"));

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

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto *saveBtn = new QPushButton(QStringLiteral("保存"), this);
    auto *saveAsBtn = new QPushButton(QStringLiteral("另存为"), this);
    auto *validateBtn = new QPushButton(QStringLiteral("校验"), this);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(saveAsBtn);
    btnRow->addWidget(validateBtn);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly))
        m_json->setPlainText(QString::fromUtf8(f.readAll()));
    else
        m_json->setPlainText(blankJsonText());
    refreshDesignFromJson();
    const QJsonObject initDoc = currentTemplate();
    m_seqMode = initDoc.contains(QStringLiteral("segments"));
    if (m_seqMode)
    {
        m_tabs->setTabVisible(1, false);
        m_tabs->setTabVisible(2, false);
        m_tabs->setCurrentIndex(0);
        refreshSeqFromJson();
    }
    else
    {
        m_tabs->setTabVisible(0, false);
    }

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
        if (m_tabs->currentWidget() == m_json)
            return;
        if (m_seqMode)
            refreshSeqFromJson();
        else
            refreshDesignFromJson();
    });
    connect(m_seqName, &QLineEdit::textEdited, this, [this] { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqDesc, &QLineEdit::textEdited, this, [this] { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqMaxsize, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqEndian, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqTimestamp, &QLineEdit::textEdited, this, [this] { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqTsUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqFields, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        if (!m_syncing)
            rebuildSeqJson();
    });
    connect(seqAdd, &QPushButton::clicked, this, [this] {
        addSeqRow(m_seqFields);
        rebuildSeqJson();
    });
    connect(seqDel, &QPushButton::clicked, this, [this] {
        const int r = m_seqFields->currentRow();
        if (r >= 0)
        {
            m_seqFields->removeRow(r);
            rebuildSeqJson();
        }
    });
    connect(m_seqSeries, &QCheckBox::toggled, this, [this](bool) {
        if (!m_syncing)
            rebuildSeqJson();
    });
    connect(m_seqSelector, &QLineEdit::textEdited, this, [this] { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqPayload, &QLineEdit::textEdited, this, [this] { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqFallback, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { if (!m_syncing) rebuildSeqJson(); });
    connect(m_seqVariants, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        if (!m_syncing)
            rebuildSeqJson();
    });
    connect(m_seqVariants, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (!m_syncing)
            loadSeqVariant(row);
    });
    connect(varAdd, &QPushButton::clicked, this, [this] {
        storeSeqVariantFields();
        QJsonObject var;
        var.insert(QStringLiteral("value"), 1);
        var.insert(QStringLiteral("name"), QStringLiteral("var%1").arg(m_seqVariantData.size() + 1));
        var.insert(QStringLiteral("segments"), QJsonArray());
        m_seqVariantData.append(var);
        loadSeqVariant(m_seqVariantData.size() - 1);
        rebuildSeqJson();
    });
    connect(varDel, &QPushButton::clicked, this, [this] {
        const int r = m_seqVariants->currentRow();
        if (r < 0)
            return;
        m_seqVariantData.removeAt(r);
        m_seqCurrentVariant = -1;
        loadSeqVariant(std::min(r, static_cast<int>(m_seqVariantData.size()) - 1));
        rebuildSeqJson();
    });
    connect(m_seqVarFields, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        if (m_syncing)
            return;
        storeSeqVariantFields();
        rebuildSeqJson();
    });

    connect(tryBtn, &QPushButton::clicked, this, [this] { tryFrame(); });
    connect(saveBtn, &QPushButton::clicked, this, [this] { save(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(validateBtn, &QPushButton::clicked, this, [this] { runValidation(); });
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
    if (o.contains(QStringLiteral("segments")))
    {
        QStringList lines;
        lines << QStringLiteral("-- PlotScope 顺序帧协议描述（由协议页生成）");
        lines << QStringLiteral("return {");
        lines << QStringLiteral("  version = 1,");
        lines << QStringLiteral("  name = %1,").arg(luaQuote(o.value(QStringLiteral("name")).toString()));
        lines << QStringLiteral("  layout = \"sequential\",");
        lines << QStringLiteral("  endian = %1,").arg(luaQuote(o.value(QStringLiteral("endian")).toString()));
        lines << QStringLiteral("  maxsize = %1,").arg(o.value(QStringLiteral("maxsize")).toInt(4096));
        const QJsonObject ts = o.value(QStringLiteral("timestamp")).toObject();
        if (!ts.isEmpty())
            lines << QStringLiteral("  timestamp = { expr = %1, unit = %2 },")
                         .arg(luaQuote(ts.value(QStringLiteral("expr")).toString()),
                              luaQuote(ts.value(QStringLiteral("unit")).toString(QStringLiteral("s"))));
        auto segmentsLua = [](const QJsonArray &segs, const QString &indent) {
            QStringList out;
            for (const QJsonValue &sv : segs)
            {
                const QJsonObject f = sv.toObject();
                QString line = indent + QStringLiteral("{ name = %1, size = %2, type = %3")
                                   .arg(luaQuote(f.value(QStringLiteral("name")).toString()),
                                        f.value(QStringLiteral("size")).isString()
                                            ? luaQuote(f.value(QStringLiteral("size")).toString())
                                            : QString::number(f.value(QStringLiteral("size")).toInt(1)),
                                        luaQuote(f.value(QStringLiteral("type")).toString(QStringLiteral("u8"))));
                if (!f.value(QStringLiteral("role")).toString().isEmpty())
                    line += QStringLiteral(", role = %1").arg(luaQuote(f.value(QStringLiteral("role")).toString()));
                if (f.contains(QStringLiteral("value")))
                    line += QStringLiteral(", value = %1").arg(f.value(QStringLiteral("value")).toDouble());
                line += QStringLiteral(" },");
                out << line;
            }
            return out;
        };
        lines << QStringLiteral("  segments = {");
        lines += segmentsLua(o.value(QStringLiteral("segments")).toArray(), QStringLiteral("    "));
        lines << QStringLiteral("  },");
        const QJsonObject series = o.value(QStringLiteral("series")).toObject();
        if (!series.isEmpty())
        {
            lines << QStringLiteral("  series = {");
            lines << QStringLiteral("    selector = %1,").arg(luaQuote(series.value(QStringLiteral("selector")).toString()));
            lines << QStringLiteral("    payload = %1,").arg(luaQuote(series.value(QStringLiteral("payload")).toString()));
            lines << QStringLiteral("    fallback = %1,").arg(luaQuote(series.value(QStringLiteral("fallback")).toString(QStringLiteral("reject"))));
            lines << QStringLiteral("    variants = {");
            for (const QJsonValue &vv : series.value(QStringLiteral("variants")).toArray())
            {
                const QJsonObject var = vv.toObject();
                lines << QStringLiteral("      { value = %1, name = %2, segments = {")
                             .arg(QString::number(var.value(QStringLiteral("value")).toInt()),
                                  luaQuote(var.value(QStringLiteral("name")).toString()));
                lines += segmentsLua(var.value(QStringLiteral("segments")).toArray(), QStringLiteral("        "));
                lines << QStringLiteral("      } },");
            }
            lines << QStringLiteral("    }");
            lines << QStringLiteral("  }");
        }
        lines << QStringLiteral("}");
        m_lua->setPlainText(lines.join(QLatin1Char('\n')));
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

    if (t.contains(QStringLiteral("segments")))
    {
        QStringList lines;
        int offset = 0;
        for (const QJsonValue &vv : t.value(QStringLiteral("segments")).toArray())
        {
            const QJsonObject seg = vv.toObject();
            const bool literal = seg.value(QStringLiteral("size")).isDouble();
            const double n = literal ? seg.value(QStringLiteral("size")).toDouble() : 1.0;
            const QString type = seg.value(QStringLiteral("type")).toString(QStringLiteral("u8"));
            int es = 1;
            if (type.contains(QStringLiteral("16"))) es = 2;
            else if (type.contains(QStringLiteral("32")) || type.contains(QStringLiteral("f32"))) es = 4;
            else if (type.contains(QStringLiteral("64")) || type.contains(QStringLiteral("f64"))) es = 8;
            if (!literal)
            {
                lines << QStringLiteral("%1 @ %2  变长(%3)")
                             .arg(seg.value(QStringLiteral("name")).toString())
                             .arg(offset)
                             .arg(seg.value(QStringLiteral("size")).toString());
                break;
            }
            const int count = static_cast<int>(n);
            const int bytes = count * es;
            QString raw;
            if (offset + bytes <= frame.size())
                raw = QString::fromLatin1(frame.mid(offset, std::min(bytes, 8)).toHex(' ').toUpper());
            lines << QStringLiteral("%1 @ %2  %3 元素 / %4 B  %5")
                         .arg(seg.value(QStringLiteral("name")).toString())
                         .arg(offset)
                         .arg(count)
                         .arg(bytes)
                         .arg(raw);
            offset += bytes;
        }
        m_result->setText(lines.join(QStringLiteral("\n")));
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
    {
        if (m_seqMode)
            rebuildSeqJson();
        else
            rebuildJsonFromDesign();
    }
    const QStringList errs = validateDocument();
    if (!errs.isEmpty())
    {
        const auto answer = QMessageBox::warning(this, QStringLiteral("协议仍有错误"),
            QStringLiteral("%1 项校验未通过，仍然保存？").arg(errs.size()),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Save)
        {
            m_result->setText(QStringLiteral("校验失败 %1 项：%2")
                                  .arg(errs.size()).arg(errs.first()));
            return;
        }
    }
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
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("另存协议"), m_path,
        QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
    if (f.isEmpty())
        return;
    m_path = f;
    save();
}

/* ---------------- 顺序段协议页 ---------------- */

namespace {

QString valText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
    {
        const double d = v.toDouble();
        if (d == std::trunc(d) && std::fabs(d) <= 255)
            return QStringLiteral("0x%1").arg(static_cast<int>(d), 2, 16, QLatin1Char('0')).toUpper();
        return QString::number(d, 'g', 12);
    }
    return QString();
}

QString checksText(const QJsonObject &seg)
{
    QStringList parts;
    if (seg.contains(QStringLiteral("value")))
        parts << valText(seg.value(QStringLiteral("value")));
    for (const QJsonValue &cv : seg.value(QStringLiteral("checks")).toArray())
    {
        const QJsonObject c = cv.toObject();
        const QString op = c.value(QStringLiteral("op")).toString(QStringLiteral("eq"));
        auto arg = [&](const QString &key) { return valText(c.value(key)); };
        if (op == QLatin1String("eq"))
            parts << arg(QStringLiteral("value"));
        else if (op == QLatin1String("range"))
            parts << QStringLiteral("range(%1,%2)").arg(arg(QStringLiteral("min")), arg(QStringLiteral("max")));
        else if (op == QLatin1String("mask"))
            parts << QStringLiteral("mask(%1,%2)").arg(arg(QStringLiteral("and")), arg(QStringLiteral("eq")));
        else if (op == QLatin1String("in"))
        {
            QStringList vs;
            for (const QJsonValue &v : c.value(QStringLiteral("values")).toArray())
                vs << valText(v);
            parts << QStringLiteral("in(%1)").arg(vs.join(QLatin1Char(',')));
        }
        else if (op == QLatin1String("crc8") || op == QLatin1String("crc16") || op == QLatin1String("crc32") ||
                 op == QLatin1String("sum8") || op == QLatin1String("sum16") || op == QLatin1String("xor8"))
        {
            QStringList over;
            for (const QJsonValue &v : c.value(QStringLiteral("over")).toArray())
                over << v.toString();
            parts << QStringLiteral("%1(%2)").arg(op, over.join(QLatin1Char(',')));
        }
        else
            parts << QStringLiteral("%1(%2)").arg(op, arg(QStringLiteral("expr")));
    }
    return parts.join(QStringLiteral("; "));
}

double numValue(const QString &s, bool *ok)
{
    return s.trimmed().toDouble(ok);
}

QJsonObject parseChecksText(const QString &text, bool *ok)
{
    QJsonObject seg;
    QJsonArray checks;
    bool sawEq = false;
    for (const QString &raw : text.split(QLatin1Char(';'), Qt::SkipEmptyParts))
    {
        const QString tok = raw.trimmed();
        if (tok.isEmpty())
            continue;
        QRegularExpression re(QStringLiteral("^([A-Za-z0-9_]+)\\s*\\((.*)\\)$"));
        const QRegularExpressionMatch m = re.match(tok);
        QString op = QStringLiteral("eq");
        QStringList args;
        if (m.hasMatch())
        {
            op = m.captured(1);
            args = m.captured(2).split(QLatin1Char(','));
        }
        else
        {
            args << tok;
        }
        for (QString &a : args)
            a = a.trimmed();

        QJsonObject c;
        c.insert(QStringLiteral("op"), op);
        if (op == QLatin1String("eq"))
        {
            if (sawEq) { *ok = false; return {}; }
            sawEq = true;
            if (args.size() != 1) { *ok = false; return {}; }
            bool isNum = false;
            const double d = numValue(args[0], &isNum);
            seg.insert(QStringLiteral("value"), isNum ? QJsonValue(d) : QJsonValue(args[0]));
        }
        else if (op == QLatin1String("in"))
        {
            QJsonArray vals;
            for (const QString &a : args)
            {
                bool isNum = false;
                vals.append(numValue(a, &isNum));
            }
            c.insert(QStringLiteral("values"), vals);
            checks.append(c);
        }
        else if (op == QLatin1String("range") || op == QLatin1String("mask"))
        {
            if (args.size() != 2) { *ok = false; return {}; }
            c.insert(op == QLatin1String("range") ? QStringLiteral("min") : QStringLiteral("and"), args[0]);
            c.insert(op == QLatin1String("range") ? QStringLiteral("max") : QStringLiteral("eq"), args[1]);
            checks.append(c);
        }
        else if (op == QLatin1String("crc8") || op == QLatin1String("crc16") || op == QLatin1String("crc32") ||
                 op == QLatin1String("sum8") || op == QLatin1String("sum16") || op == QLatin1String("xor8"))
        {
            QJsonArray over;
            for (const QString &a : args)
                if (!a.isEmpty())
                    over.append(a);
            c.insert(QStringLiteral("over"), over);
            checks.append(c);
        }
        else
        {
            if (args.size() != 1) { *ok = false; return {}; }
            c.insert(QStringLiteral("expr"), args[0]);
            checks.append(c);
        }
    }
    seg.insert(QStringLiteral("checks"), checks);
    return seg;
}

int protocolTypeBytes(const QString &type)
{
    const QString t = type.toLower();
    if (t.startsWith(QLatin1String("u16")) || t.startsWith(QLatin1String("i16"))) return 2;
    if (t.startsWith(QLatin1String("u32")) || t.startsWith(QLatin1String("i32")) || t.startsWith(QLatin1String("f32"))) return 4;
    if (t.startsWith(QLatin1String("u64")) || t.startsWith(QLatin1String("i64")) || t.startsWith(QLatin1String("f64"))) return 8;
    return 1;
}

bool protocolLiteralSize(const QJsonValue &v, int *out)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (d != std::trunc(d) || d < 1 || d > 65535)
        return false;
    *out = static_cast<int>(d);
    return true;
}

int protocolOpWidth(const QString &op)
{
    if (op == QLatin1String("crc16") || op == QLatin1String("sum16")) return 2;
    if (op == QLatin1String("crc32")) return 4;
    if (op == QLatin1String("crc8") || op == QLatin1String("sum8") ||
        op == QLatin1String("xor8") || op == QLatin1String("parity")) return 1;
    return 0;
}

QStringList validateProtocolObject(const QJsonObject &o)
{
    QStringList errs;
    if (o.isEmpty())
    {
        errs << QStringLiteral("JSON 为空或无法解析");
        return errs;
    }
    if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        errs << QStringLiteral("协议 name 不能为空");
    if (o.value(QStringLiteral("layout")).toString() != QLatin1String("sequential"))
        errs << QStringLiteral("新协议 layout 必须是 sequential");
    const int maxsize = o.value(QStringLiteral("maxsize")).toInt(4096);
    if (maxsize < 2 || maxsize > 65535)
        errs << QStringLiteral("maxsize 必须在 2..65535");
    const QJsonArray segArr = o.value(QStringLiteral("segments")).toArray();
    if (segArr.isEmpty())
        errs << QStringLiteral("segments 不能为空");
    if (o.value(QStringLiteral("endian")).toString() != QLatin1String("big") &&
        o.value(QStringLiteral("endian")).toString() != QLatin1String("little") &&
        o.contains(QStringLiteral("endian")))
        errs << QStringLiteral("endian 只能是 little 或 big");

    QSet<QString> names;
    QMap<QString, int> order;
    QMap<QString, int> fixedBytes;
    QMap<QString, QString> roles;
    int index = 0;
    for (const QJsonValue &sv : segArr)
    {
        const QJsonObject seg = sv.toObject();
        const QString name = seg.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
        {
            errs << QStringLiteral("第 %1 段缺少 name").arg(index + 1);
            ++index;
            continue;
        }
        if (names.contains(name))
            errs << QStringLiteral("段名重复：%1").arg(name);
        names.insert(name);
        order.insert(name, index++);
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(name).hasMatch())
            errs << QStringLiteral("段名只能是标识符：%1").arg(name);
        int n = 0;
        if (protocolLiteralSize(seg.value(QStringLiteral("size")), &n))
            fixedBytes.insert(name, n * protocolTypeBytes(seg.value(QStringLiteral("type")).toString()));
        else if (!seg.value(QStringLiteral("size")).isString() ||
                 seg.value(QStringLiteral("size")).toString().trimmed().isEmpty())
            errs << QStringLiteral("%1 的 size 必须是正整数或表达式").arg(name);
        const QString type = seg.value(QStringLiteral("type")).toString().toLower();
        const QStringList types = {QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"),
            QStringLiteral("i16"), QStringLiteral("u32"), QStringLiteral("i32"), QStringLiteral("u64"),
            QStringLiteral("i64"), QStringLiteral("f32"), QStringLiteral("f64"), QStringLiteral("bool"),
            QStringLiteral("bcd"), QStringLiteral("str"), QStringLiteral("string"),
            QStringLiteral("ascii"), QStringLiteral("char")};
        if (!types.contains(type))
            errs << QStringLiteral("%1 的类型未知：%2").arg(name, type);
        const QString role = seg.value(QStringLiteral("role")).toString();
        if (!role.isEmpty())
        {
            const QStringList allowed = {QStringLiteral("sync"), QStringLiteral("length"),
                QStringLiteral("payload"), QStringLiteral("checksum"), QStringLiteral("data")};
            if (!allowed.contains(role))
                errs << QStringLiteral("%1 的 role 未知：%2").arg(name, role);
            else if ((role == QLatin1String("length") || role == QLatin1String("payload") ||
                      role == QLatin1String("checksum")) && roles.contains(role))
                errs << QStringLiteral("role 重复：%1").arg(role);
            else
                roles.insert(role, name);
            if (role == QLatin1String("payload") && type != QLatin1String("u8"))
                errs << QStringLiteral("payload 段 %1 必须是 u8").arg(name);
            if (role == QLatin1String("length") && seg.value(QStringLiteral("size")).isDouble() &&
                seg.value(QStringLiteral("size")).toDouble() > 1)
                errs << QStringLiteral("length 段 %1 应为单元素").arg(name);
        }
        if (seg.contains(QStringLiteral("value")) && seg.value(QStringLiteral("value")).isUndefined())
            errs << QStringLiteral("%1 的 value 无效").arg(name);

        const QJsonArray checks = seg.value(QStringLiteral("checks")).toArray();
        for (const QJsonValue &cv : checks)
        {
            const QJsonObject c = cv.toObject();
            const QString op = c.value(QStringLiteral("op")).toString(QStringLiteral("eq"));
            const QStringList pred = {QStringLiteral("eq"), QStringLiteral("ne"), QStringLiteral("lt"),
                QStringLiteral("le"), QStringLiteral("gt"), QStringLiteral("ge"), QStringLiteral("in"),
                QStringLiteral("range"), QStringLiteral("mask")};
            const QStringList seq = {QStringLiteral("crc8"), QStringLiteral("crc16"), QStringLiteral("crc32"),
                QStringLiteral("sum8"), QStringLiteral("sum16"), QStringLiteral("xor8"), QStringLiteral("parity")};
            if (!pred.contains(op) && !seq.contains(op))
            {
                errs << QStringLiteral("%1 的校验算子未知：%2").arg(name, op);
                continue;
            }
            if (seq.contains(op))
            {
                const QJsonArray over = c.value(QStringLiteral("over")).toArray();
                if (over.isEmpty())
                    errs << QStringLiteral("%1 的 %2 缺少 over").arg(name, op);
                for (const QJsonValue &ov : over)
                {
                    const QString r = ov.toString();
                    const QStringList parts = r.split(QLatin1Char(':'));
                    if (parts.size() > 2 || parts.first().trimmed().isEmpty() ||
                        (parts.size() == 2 && parts.last().trimmed().isEmpty()))
                    {
                        errs << QStringLiteral("%1 的 over 区间非法：%2").arg(name, r);
                        continue;
                    }
                    for (const QString &p : parts)
                    {
                        if (!names.contains(p.trimmed()))
                            errs << QStringLiteral("%1 的 over 引用了未知段：%2").arg(name, p);
                    }
                    if (parts.size() == 2)
                    {
                        const int a = order.value(parts[0].trimmed(), -1);
                        const int b = order.value(parts[1].trimmed(), -1);
                        if (a >= 0 && b >= 0 && a > b)
                            errs << QStringLiteral("%1 的 over 区间顺序反了：%2").arg(name, r);
                    }
                }
                const int need = protocolOpWidth(op);
                int elem = 0;
                if (protocolLiteralSize(seg.value(QStringLiteral("size")), &elem))
                {
                    const int have = elem * protocolTypeBytes(type);
                    if (need > 0 && have != need)
                        errs << QStringLiteral("%1 的字段宽度 %2B 不匹配 %3 输出 %4B")
                                    .arg(name).arg(have).arg(op).arg(need);
                }
            }
            else if (op == QLatin1String("in"))
            {
                if (c.value(QStringLiteral("values")).toArray().isEmpty())
                    errs << QStringLiteral("%1 的 in 缺少 values").arg(name);
            }
            else if (op == QLatin1String("range"))
            {
                if (!c.contains(QStringLiteral("min")) || !c.contains(QStringLiteral("max")))
                    errs << QStringLiteral("%1 的 range 缺少 min/max").arg(name);
            }
            else if (op == QLatin1String("mask"))
            {
                if (!c.contains(QStringLiteral("and")) || !c.contains(QStringLiteral("eq")))
                    errs << QStringLiteral("%1 的 mask 缺少 and/eq").arg(name);
            }
            else if (!c.contains(QStringLiteral("value")) && !c.contains(QStringLiteral("expr")))
                errs << QStringLiteral("%1 的 %2 缺少比较值").arg(name, op);
        }
    }

    const QJsonObject ts = o.value(QStringLiteral("timestamp")).toObject();
    if (!ts.isEmpty())
    {
        if (ts.value(QStringLiteral("expr")).toString().trimmed().isEmpty())
            errs << QStringLiteral("timestamp.expr 不能为空");
        const QString unit = ts.value(QStringLiteral("unit")).toString(QStringLiteral("s"));
        if (unit != QLatin1String("s") && unit != QLatin1String("ms") && unit != QLatin1String("us"))
            errs << QStringLiteral("timestamp.unit 只能是 s/ms/us");
    }

    const QJsonObject series = o.value(QStringLiteral("series")).toObject();
    if (!series.isEmpty())
    {
        const QString selector = series.value(QStringLiteral("selector")).toString().trimmed();
        if (selector.isEmpty())
            errs << QStringLiteral("series.selector 不能为空");
        const QString payload = series.value(QStringLiteral("payload")).toString().trimmed();
        if (payload.isEmpty())
            errs << QStringLiteral("series.payload 不能为空");
        else if (!names.contains(payload))
            errs << QStringLiteral("series.payload 引用了未知段：%1").arg(payload);
        else if (roles.value(QLatin1String("payload")) != payload)
            errs << QStringLiteral("series.payload 必须指向 role=payload 的段");
        const QString fallback = series.value(QStringLiteral("fallback")).toString(QStringLiteral("reject"));
        if (fallback != QLatin1String("reject") && fallback != QLatin1String("raw"))
            errs << QStringLiteral("series.fallback 只能是 reject 或 raw");
        const QJsonArray vars = series.value(QStringLiteral("variants")).toArray();
        if (vars.isEmpty())
            errs << QStringLiteral("series 至少需要一个变体");
        QSet<QString> varNames;
        QSet<QString> varValues;
        int valueCount = 0;
        auto addVariantKey = [&](const QJsonValue &x) {
            varValues.insert(x.isString() ? QStringLiteral("s:") + x.toString()
                                           : QStringLiteral("n:") + QString::number(x.toDouble(), 'g', 17));
            ++valueCount;
        };
        int payloadBytes = -1;
        if (!payload.isEmpty())
            payloadBytes = fixedBytes.value(payload, -1);
        for (const QJsonValue &vv : vars)
        {
            const QJsonObject var = vv.toObject();
            const QString vn = var.value(QStringLiteral("name")).toString().trimmed();
            if (vn.isEmpty())
                errs << QStringLiteral("变体缺少 name");
            else if (varNames.contains(vn))
                errs << QStringLiteral("变体名重复：%1").arg(vn);
            varNames.insert(vn);
            if (var.contains(QStringLiteral("values")))
            {
                for (const QJsonValue &x : var.value(QStringLiteral("values")).toArray())
                    addVariantKey(x);
            }
            else if (var.contains(QStringLiteral("value")))
            {
                addVariantKey(var.value(QStringLiteral("value")));
            }
            else
                errs << QStringLiteral("变体 %1 缺少 value/values").arg(vn);
            const QJsonArray vsegs = var.value(QStringLiteral("segments")).toArray();
            int vbytes = 0;
            bool vvariable = false;
            for (const QJsonValue &vsv : vsegs)
            {
                const QJsonObject vseg = vsv.toObject();
                int n = 0;
                if (!protocolLiteralSize(vseg.value(QStringLiteral("size")), &n))
                {
                    vvariable = true;
                    continue;
                }
                vbytes += n * protocolTypeBytes(vseg.value(QStringLiteral("type")).toString());
            }
            if (payloadBytes >= 0 && !vvariable && vbytes != payloadBytes)
                errs << QStringLiteral("变体 %1 的段总字节 %2 与 payload %3 不一致")
                            .arg(vn).arg(vbytes).arg(payloadBytes);
        }
        if (valueCount > 0 && varValues.size() != valueCount)
            errs << QStringLiteral("变体 value/values 有重复");
    }

    int fixedTotal = 0;
    bool hasVariable = false;
    for (int b : fixedBytes)
        fixedTotal += b;
    for (const QJsonValue &sv : segArr)
        if (!sv.toObject().value(QStringLiteral("size")).isDouble())
            hasVariable = true;
    if (!hasVariable && fixedTotal > maxsize)
        errs << QStringLiteral("固定段总字节 %1 超过 maxsize %2").arg(fixedTotal).arg(maxsize);
    return errs;
}

} // namespace

QString ProtocolEditorDialog::blankJsonText()
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("name"), QStringLiteral("新协议"));
    o.insert(QStringLiteral("description"), QString());
    o.insert(QStringLiteral("layout"), QStringLiteral("sequential"));
    o.insert(QStringLiteral("endian"), QStringLiteral("little"));
    o.insert(QStringLiteral("maxsize"), 256);
    QJsonArray segs;
    auto seg = [](const QString &name, const QJsonValue &size, const QString &type,
                  const QString &role, const QJsonValue &value = QJsonValue()) {
        QJsonObject s;
        s.insert(QStringLiteral("name"), name);
        s.insert(QStringLiteral("size"), size);
        s.insert(QStringLiteral("type"), type);
        s.insert(QStringLiteral("role"), role);
        if (!value.isNull() && !value.isUndefined())
            s.insert(QStringLiteral("value"), value);
        return s;
    };
    segs.append(seg(QStringLiteral("sync1"), 1, QStringLiteral("u8"), QStringLiteral("sync"), 0xAA));
    segs.append(seg(QStringLiteral("sync2"), 1, QStringLiteral("u8"), QStringLiteral("sync"), 0x55));
    segs.append(seg(QStringLiteral("length"), 1, QStringLiteral("u16"), QStringLiteral("length")));
    segs.append(seg(QStringLiteral("payload"), QStringLiteral("length"), QStringLiteral("u8"), QStringLiteral("payload")));
    QJsonObject crc = seg(QStringLiteral("crc"), 1, QStringLiteral("u8"), QStringLiteral("checksum"));
    QJsonArray checks;
    QJsonObject check;
    check.insert(QStringLiteral("op"), QStringLiteral("crc8"));
    QJsonArray over;
    over.append(QStringLiteral("sync1:payload"));
    check.insert(QStringLiteral("over"), over);
    checks.append(check);
    crc.insert(QStringLiteral("checks"), checks);
    segs.append(crc);
    o.insert(QStringLiteral("segments"), segs);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

QStringList ProtocolEditorDialog::validateFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {QStringLiteral("无法读取：%1").arg(path)};
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return {QStringLiteral("JSON 解析失败：%1").arg(pe.errorString())};
    return validateProtocolObject(doc.object());
}

QStringList ProtocolEditorDialog::validateDocument() const
{
    return validateProtocolObject(currentTemplate());
}

void ProtocolEditorDialog::runValidation()
{
    if (m_tabs->currentWidget() != m_json)
    {
        if (m_seqMode)
            rebuildSeqJson();
        else
            rebuildJsonFromDesign();
    }
    const QStringList errs = validateDocument();
    if (errs.isEmpty())
    {
        m_result->setStyleSheet(QStringLiteral("color:#3fb950;"));
        m_result->setText(QStringLiteral("校验通过：结构、段、校验与系列协议一致"));
        return;
    }
    m_result->setStyleSheet(QStringLiteral("color:#f85149;"));
    m_result->setText(QStringLiteral("校验失败 %1 项：%2").arg(errs.size()).arg(errs.first()));
    QMessageBox box(QMessageBox::Warning, QStringLiteral("协议校验失败"),
                    QStringLiteral("共 %1 项问题，修正后再保存。").arg(errs.size()),
                    QMessageBox::Ok, this);
    box.setDetailedText(errs.join(QStringLiteral("\n")));
    box.exec();
}

void ProtocolEditorDialog::setupSeqTables()
{
}

void ProtocolEditorDialog::populateSeqTable(QTableWidget *t, const QJsonArray &segments)
{
    t->setRowCount(0);
    for (const QJsonValue &vv : segments)
    {
        const QJsonObject seg = vv.toObject();
        const int r = t->rowCount();
        t->insertRow(r);
        QTableWidgetItem *nameItem = new QTableWidgetItem(seg.value(QStringLiteral("name")).toString());
        QJsonObject extra = seg;
        extra.remove(QStringLiteral("name"));
        extra.remove(QStringLiteral("size"));
        extra.remove(QStringLiteral("type"));
        extra.remove(QStringLiteral("value"));
        extra.remove(QStringLiteral("checks"));
        nameItem->setData(Qt::UserRole, QJsonDocument(extra).toJson(QJsonDocument::Compact));
        t->setItem(r, 0, nameItem);
        const QJsonValue sz = seg.value(QStringLiteral("size"));
        t->setItem(r, 1, new QTableWidgetItem(sz.isString() ? sz.toString() : QString::number(sz.toInt(1))));
        t->setItem(r, 2, new QTableWidgetItem(seg.value(QStringLiteral("type")).toString(QStringLiteral("u8"))));
        t->setItem(r, 3, new QTableWidgetItem(checksText(seg)));
    }
}

QJsonArray ProtocolEditorDialog::seqTableToJson(QTableWidget *t) const
{
    QJsonArray arr;
    for (int r = 0; r < t->rowCount(); ++r)
    {
        QTableWidgetItem *nameItem = t->item(r, 0);
        const QString name = nameItem ? nameItem->text().trimmed() : QString();
        if (name.isEmpty())
            continue;
        QJsonObject seg;
        if (nameItem)
        {
            const QJsonDocument d = QJsonDocument::fromJson(nameItem->data(Qt::UserRole).toByteArray());
            if (d.isObject())
                seg = d.object();
        }
        seg.insert(QStringLiteral("name"), name);
        const QString sz = t->item(r, 1) ? t->item(r, 1)->text().trimmed() : QStringLiteral("1");
        bool isNum = false;
        const double n = sz.toDouble(&isNum);
        seg.insert(QStringLiteral("size"), isNum ? QJsonValue(n) : QJsonValue(sz));
        seg.insert(QStringLiteral("type"), t->item(r, 2) ? t->item(r, 2)->text().trimmed() : QStringLiteral("u8"));
        bool ok = true;
        QJsonObject parsed = parseChecksText(t->item(r, 3) ? t->item(r, 3)->text() : QString(), &ok);
        if (!ok)
            parsed = QJsonObject();
        for (auto it = parsed.begin(); it != parsed.end(); ++it)
            seg.insert(it.key(), it.value());
        if (!seg.contains(QStringLiteral("role")))
        {
            if (name.compare(QLatin1String("payload"), Qt::CaseInsensitive) == 0)
                seg.insert(QStringLiteral("role"), QStringLiteral("payload"));
            else if (name.compare(QLatin1String("length"), Qt::CaseInsensitive) == 0)
                seg.insert(QStringLiteral("role"), QStringLiteral("length"));
            else if (name.startsWith(QLatin1String("sync"), Qt::CaseInsensitive))
                seg.insert(QStringLiteral("role"), QStringLiteral("sync"));
            else if (name.contains(QLatin1String("crc"), Qt::CaseInsensitive))
                seg.insert(QStringLiteral("role"), QStringLiteral("checksum"));
        }
        arr.append(seg);
    }
    return arr;
}

void ProtocolEditorDialog::addSeqRow(QTableWidget *t)
{
    const int r = t->rowCount();
    t->insertRow(r);
    t->setItem(r, 0, new QTableWidgetItem(QStringLiteral("field%1").arg(r + 1)));
    t->setItem(r, 1, new QTableWidgetItem(QStringLiteral("1")));
    t->setItem(r, 2, new QTableWidgetItem(QStringLiteral("u8")));
    t->setItem(r, 3, new QTableWidgetItem(QString()));
    t->setCurrentCell(r, 0);
}

void ProtocolEditorDialog::refreshSeqFromJson()
{
    const QJsonObject o = currentTemplate();
    if (o.isEmpty() || !o.contains(QStringLiteral("segments")))
        return;
    m_syncing = true;
    m_seqName->setText(o.value(QStringLiteral("name")).toString());
    m_seqDesc->setText(o.value(QStringLiteral("description")).toString());
    m_seqMaxsize->setValue(o.value(QStringLiteral("maxsize")).toInt(4096));
    m_seqEndian->setCurrentText(o.value(QStringLiteral("endian")).toString(QStringLiteral("little")));
    const QJsonValue tv = o.value(QStringLiteral("timestamp"));
    m_seqTsExtra = tv.toObject();
    const QString texpr = tv.isObject() ? tv.toObject().value(QStringLiteral("expr")).toString() : tv.toString();
    m_seqTimestamp->setText(texpr);
    m_seqTsUnit->setCurrentText(tv.isObject() ? tv.toObject().value(QStringLiteral("unit")).toString(QStringLiteral("s"))
                                              : QStringLiteral("s"));
    populateSeqTable(m_seqFields, o.value(QStringLiteral("segments")).toArray());

    const QJsonObject series = o.value(QStringLiteral("series")).toObject();
    m_seqSeries->setChecked(!series.isEmpty());
    m_seqSelector->setText(series.value(QStringLiteral("selector")).toString());
    m_seqPayload->setText(series.value(QStringLiteral("payload")).toString());
    m_seqFallback->setCurrentText(series.value(QStringLiteral("fallback")).toString(QStringLiteral("reject")));
    m_seqVariantData = series.value(QStringLiteral("variants")).toArray();
    m_seqCurrentVariant = -1;
    m_seqVariants->setRowCount(0);
    for (const QJsonValue &vv : m_seqVariantData)
    {
        const QJsonObject var = vv.toObject();
        const int r = m_seqVariants->rowCount();
        m_seqVariants->insertRow(r);
        QString valueText;
        if (var.contains(QStringLiteral("values")))
        {
            QStringList vals;
            for (const QJsonValue &x : var.value(QStringLiteral("values")).toArray())
                vals << (x.isString() ? x.toString() : QString::number(x.toDouble(), 'g', 12));
            valueText = vals.join(QStringLiteral(","));
        }
        else
        {
            const QJsonValue v = var.value(QStringLiteral("value"));
            valueText = v.isString() ? v.toString() : QString::number(v.toDouble(), 'g', 12);
        }
        m_seqVariants->setItem(r, 0, new QTableWidgetItem(valueText));
        m_seqVariants->setItem(r, 1, new QTableWidgetItem(var.value(QStringLiteral("name")).toString()));
    }
    m_syncing = false;
    loadSeqVariant(m_seqVariantData.isEmpty() ? -1 : 0);
    updateSeqPreview();
    m_result->setText(QStringLiteral("顺序协议：%1 个段，%2 个变体")
                          .arg(m_seqFields->rowCount()).arg(m_seqVariantData.size()));
    rebuildLua();
}

void ProtocolEditorDialog::storeSeqVariantFields()
{
    if (m_seqCurrentVariant < 0 || m_seqCurrentVariant >= m_seqVariantData.size())
        return;
    QJsonObject var = m_seqVariantData.at(m_seqCurrentVariant).toObject();
    var.insert(QStringLiteral("segments"), seqTableToJson(m_seqVarFields));
    m_seqVariantData.replace(m_seqCurrentVariant, var);
}

void ProtocolEditorDialog::loadSeqVariant(int row)
{
    storeSeqVariantFields();
    if (row < 0 || row >= m_seqVariantData.size())
    {
        m_seqCurrentVariant = -1;
        m_seqVarFields->setRowCount(0);
        return;
    }
    m_seqCurrentVariant = row;
    m_syncing = true;
    populateSeqTable(m_seqVarFields, m_seqVariantData.at(row).toObject().value(QStringLiteral("segments")).toArray());
    m_syncing = false;
}

void ProtocolEditorDialog::updateSeqPreview()
{
    int literalBytes = 0;
    bool unknown = false;
    for (int r = 0; r < m_seqFields->rowCount(); ++r)
    {
        bool ok = false;
        const double n = m_seqFields->item(r, 1) ? m_seqFields->item(r, 1)->text().toDouble(&ok) : 0;
        const QString type = m_seqFields->item(r, 2) ? m_seqFields->item(r, 2)->text().toLower() : QStringLiteral("u8");
        int es = 1;
        if (type.contains(QStringLiteral("16"))) es = 2;
        else if (type.contains(QStringLiteral("32")) || type.contains(QStringLiteral("f32"))) es = 4;
        else if (type.contains(QStringLiteral("64")) || type.contains(QStringLiteral("f64"))) es = 8;
        if (ok) literalBytes += static_cast<int>(n) * es;
        else unknown = true;
    }
    m_seqPreview->setText(QStringLiteral("%1 个段，固定部分约 %2 字节%3；偏移由长度累加推导")
                              .arg(m_seqFields->rowCount())
                              .arg(literalBytes)
                              .arg(unknown ? QStringLiteral("（含表达式长度）") : QString()));
}

void ProtocolEditorDialog::rebuildSeqJson()
{
    if (m_syncing)
        return;
    storeSeqVariantFields();
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("name"), m_seqName->text().trimmed());
    o.insert(QStringLiteral("description"), m_seqDesc->text());
    o.insert(QStringLiteral("layout"), QStringLiteral("sequential"));
    o.insert(QStringLiteral("endian"), m_seqEndian->currentText());
    o.insert(QStringLiteral("maxsize"), m_seqMaxsize->value());
    o.insert(QStringLiteral("segments"), seqTableToJson(m_seqFields));

    if (!m_seqTimestamp->text().trimmed().isEmpty())
    {
        QJsonObject ts = m_seqTsExtra;
        ts.insert(QStringLiteral("expr"), m_seqTimestamp->text().trimmed());
        ts.insert(QStringLiteral("unit"), m_seqTsUnit->currentText());
        if (!ts.contains(QStringLiteral("origin")))
            ts.insert(QStringLiteral("origin"), QStringLiteral("first_frame"));
        if (!ts.contains(QStringLiteral("fallback")))
            ts.insert(QStringLiteral("fallback"), QStringLiteral("reject"));
        o.insert(QStringLiteral("timestamp"), ts);
    }

    if (m_seqSeries->isChecked())
    {
        QJsonObject series;
        series.insert(QStringLiteral("selector"), m_seqSelector->text().trimmed());
        series.insert(QStringLiteral("payload"), m_seqPayload->text().trimmed());
        series.insert(QStringLiteral("fallback"), m_seqFallback->currentText());
        QJsonArray variants;
        for (int r = 0; r < m_seqVariants->rowCount(); ++r)
        {
            QJsonObject var = r < m_seqVariantData.size() ? m_seqVariantData.at(r).toObject() : QJsonObject();
            var.insert(QStringLiteral("name"), m_seqVariants->item(r, 1) ? m_seqVariants->item(r, 1)->text() : QString());
            QStringList vals = (m_seqVariants->item(r, 0) ? m_seqVariants->item(r, 0)->text() : QString())
                                   .split(QLatin1Char(','), Qt::SkipEmptyParts);
            auto toValue = [](const QString &text) -> QJsonValue {
                bool ok = false;
                const double d = text.trimmed().toDouble(&ok);
                return ok ? QJsonValue(d) : QJsonValue(text.trimmed());
            };
            if (vals.size() == 1)
            {
                var.remove(QStringLiteral("values"));
                var.insert(QStringLiteral("value"), toValue(vals[0]));
            }
            else
            {
                var.remove(QStringLiteral("value"));
                QJsonArray varr;
                for (const QString &v : vals)
                    varr.append(toValue(v));
                var.insert(QStringLiteral("values"), varr);
            }
            if (!var.contains(QStringLiteral("segments")))
                var.insert(QStringLiteral("segments"), QJsonArray());
            variants.append(var);
        }
        series.insert(QStringLiteral("variants"), variants);
        o.insert(QStringLiteral("series"), series);
    }

    m_syncing = true;
    m_json->setPlainText(QJsonDocument(o).toJson(QJsonDocument::Indented));
    m_syncing = false;
    updateSeqPreview();
    rebuildLua();
}
