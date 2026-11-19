#include "ProtocolEditorDialog.h"

#include "ThemeManager.h"
#include "TransformEditorDialog.h"
#include "AppContext.h"
#include "Transform.h"

#include "ProtocolIntentHandler.h"
#include "ProtocolTemplateStore.h"

#include "IIntentExecutor.h"
#include "Intent.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::gui
{

namespace
{
// 段表列：沿用老编辑器的「字段 / 长度 / 类型 / 取值」，末尾加一列"其它键"（只读摘要），
// 让 scale / bit / digest / enum 这些结构化键可见但不挤进可编辑格——具体值在"段详情"里改。
enum SegCol
{
    SegField = 0,
    SegCount,
    SegType,
    SegValue,
    SegOther,
    SegColCount
};

QJsonObject objectFromItem(const QTableWidgetItem *item)
{
    if (!item)
        return {};
    const QJsonDocument d = QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray());
    return d.isObject() ? d.object() : QJsonObject{};
}

void storeObjectOnItem(QTableWidgetItem *item, const QJsonObject &o)
{
    if (!item)
        return;
    item->setData(Qt::UserRole, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// 行顺序就是协议语义（段按顺序读），所以拖拽必须能真搬行。
// 老编辑器的 ReorderTable 原样搬：段表里所有单元格都是纯文本 item，没有 cellWidget，
// 因此整行 takeItem/insertRow 不会丢控件。
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

void setupSegTable(QTableWidget *t)
{
    t->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("长度"),
                                  QStringLiteral("类型"), QStringLiteral("取值"),
                                  QStringLiteral("其它键")});
    t->horizontalHeader()->setStretchLastSection(true);
    t->setColumnWidth(SegField, 170);
    t->setColumnWidth(SegCount, 110);
    t->setColumnWidth(SegType, 100);
    t->setColumnWidth(SegValue, 110);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setDragEnabled(true);
    t->setAcceptDrops(true);
    t->viewport()->setAcceptDrops(true);
    t->setDropIndicatorShown(true);
    t->setDragDropMode(QAbstractItemView::InternalMove);
    t->setDefaultDropAction(Qt::MoveAction);
}

// 只读摘要：列出除"字段/长度/类型/取值"以外的键。
QString otherKeysText(const QJsonObject &seg)
{
    QStringList keys;
    for (auto it = seg.begin(); it != seg.end(); ++it)
    {
        const QString k = it.key();
        if (k == QLatin1String("name") || k == QLatin1String("type") ||
            k == QLatin1String("count") || k == QLatin1String("expect"))
            continue;
        keys << k;
    }
    return keys.join(QStringLiteral(", "));
}
} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(QWidget *parent, AppContext *appCtx,
                                          nextpilot::plotscope::core::IIntentExecutor *executor)
    : QDialog(parent), m_appCtx(appCtx), m_executor(executor)
{
    setWindowTitle(QStringLiteral("协议编辑器"));
    resize(1060, 900);  // 与老编辑器同尺寸：段表 + 变体段表都要留出足够行高

    // --- 顶部：文件操作 ---
    auto *top = new QHBoxLayout;
    auto *newBtn = new QPushButton(QStringLiteral("新建"));
    auto *openBtn = new QPushButton(QStringLiteral("打开"));
    m_saveBtn = new QPushButton(QStringLiteral("保存"));
    auto *validateBtn = new QPushButton(QStringLiteral("校验"));
    auto *checksumBtn = new QPushButton(QStringLiteral("Checksum 变换…"));
    checksumBtn->setToolTip(QStringLiteral("为 checksum 配置算子 / 数学表达式 / Lua（按能力过滤 + 在线调试）"));
    top->addWidget(newBtn);
    top->addWidget(openBtn);
    top->addWidget(m_saveBtn);
    top->addWidget(validateBtn);
    top->addWidget(checksumBtn);
    top->addStretch(1);
    connect(checksumBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::configureChecksumTransform);

    // --- 主体：左「协议 / JSON」页，右 校验与试算 ---
    auto *body = new QSplitter(Qt::Horizontal);

    auto *left = new QWidget;
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget;

    auto *designPage = new QWidget;
    m_tabs->addTab(designPage, QStringLiteral("协议"));

    auto *jsonPage = new QWidget;
    auto *jsonLay = new QVBoxLayout(jsonPage);
    jsonLay->setContentsMargins(0, 0, 0, 0);
    m_json = new QPlainTextEdit;
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_json->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    jsonLay->addWidget(m_json, 1);
    m_tabs->addTab(jsonPage, QStringLiteral("JSON"));

    leftLay->addWidget(m_tabs, 1);
    buildDesignPage();  // 页签建好后再往里填控件

    auto *right = new QWidget;
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    m_validation = new QLabel(QStringLiteral("尚未校验"));
    m_validation->setWordWrap(true);
    rightLay->addWidget(new QLabel(QStringLiteral("校验结果")));
    rightLay->addWidget(m_validation);
    rightLay->addWidget(new QLabel(QStringLiteral("样例帧（hex，如 AA 55 0C 00 01）")));
    m_hex = new QPlainTextEdit;
    m_hex->setMaximumHeight(70);
    m_hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    rightLay->addWidget(m_hex);
    auto *previewBtn = new QPushButton(QStringLiteral("试算单帧"));
    rightLay->addWidget(previewBtn);
    m_preview = new QTreeWidget;
    m_preview->setHeaderLabels({QStringLiteral("字段"), QStringLiteral("值")});
    m_preview->header()->setStretchLastSection(true);
    m_preview->setColumnWidth(0, 150);
    rightLay->addWidget(m_preview, 1);

    body->addWidget(left);
    body->addWidget(right);
    body->setStretchFactor(0, 4);
    body->setStretchFactor(1, 3);

    // --- 底部 ---
    m_status = new QLabel(QStringLiteral("新建协议"));
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    bottom->addWidget(closeBtn);

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(body, 1);
    root->addLayout(bottom);

    connect(newBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::newDoc);
    connect(openBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::openDoc);
    connect(m_saveBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::saveDoc);
    connect(validateBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::validateDoc);
    connect(previewBtn, &QPushButton::clicked, this, &ProtocolEditorDialog::previewFrame);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // 切页签即"落表"：去 JSON 页先把控件写成文本；回协议页再从文本重建。
    // 这样两边永远只有一份真数据。
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_syncing)
            return;
        if (index == 1)
            commitDesign();
        else
            refreshFromJson();
    });

    newDoc();
    applyTheme();
}

ProtocolEditorDialog::~ProtocolEditorDialog() = default;

// ---------------------------------------------------------------- 协议页布局

void ProtocolEditorDialog::buildDesignPage()
{
    QWidget *page = m_tabs->widget(0);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // ---- 协议属性（竖排，沿用老编辑器） ----
    auto *head = new QGroupBox(QStringLiteral("协议"), page);
    auto *form = new QFormLayout(head);
    m_name = new QLineEdit(head);
    m_desc = new QLineEdit(head);
    m_mode = new QComboBox(head);
    m_mode->addItems({QStringLiteral("bin"), QStringLiteral("text")});
    m_layout = new QComboBox(head);
    m_layout->addItems({QStringLiteral("sequential"), QStringLiteral("line"),
                        QStringLiteral("csv"), QStringLiteral("kv")});
    m_maxsize = new QSpinBox(head);
    m_maxsize->setRange(0, 65535);
    m_maxsize->setSpecialValueText(QStringLiteral("未设"));
    m_endian = new QComboBox(head);
    m_endian->addItems({QStringLiteral("little"), QStringLiteral("big")});
    m_tsExpr = new QLineEdit(head);
    m_tsExpr->setPlaceholderText(QStringLiteral("如 time_s 或 day*24*3600 + sec + ms/1000"));
    m_tsUnit = new QComboBox(head);
    m_tsUnit->addItems({QString(), QStringLiteral("s"), QStringLiteral("ms"),
                        QStringLiteral("us"), QStringLiteral("ns")});
    auto *tsRow = new QHBoxLayout;
    tsRow->setContentsMargins(0, 0, 0, 0);
    tsRow->addWidget(m_tsExpr, 1);
    tsRow->addWidget(new QLabel(QStringLiteral("单位"), head));
    tsRow->addWidget(m_tsUnit);
    auto *tsWrap = new QWidget(head);
    tsWrap->setLayout(tsRow);

    form->addRow(QStringLiteral("名称"), m_name);
    form->addRow(QStringLiteral("描述"), m_desc);
    form->addRow(QStringLiteral("模式"), m_mode);
    form->addRow(QStringLiteral("布局"), m_layout);
    form->addRow(QStringLiteral("最大帧长"), m_maxsize);
    form->addRow(QStringLiteral("字节序"), m_endian);
    form->addRow(QStringLiteral("时间戳表达式"), tsWrap);
    lay->addWidget(head);

    // ---- 段表工具栏 ----
    auto *segBar = new QHBoxLayout;
    auto *segAdd = new QPushButton(QStringLiteral("+ 段"), page);
    auto *segDel = new QPushButton(QStringLiteral("− 段"), page);
    auto *segHint = new QLabel(
        QStringLiteral("长度=元素个数（可写前序段名或表达式），类型=元素类型；拖拽行调整顺序；"
                       "其它键在「段详情」里编辑"), page);
    segHint->setWordWrap(true);
    segBar->addWidget(segAdd);
    segBar->addWidget(segDel);
    segBar->addWidget(segHint, 1);
    lay->addLayout(segBar);

    auto *segTable = new ReorderTable(0, SegColCount, page);
    m_segs = segTable;
    setupSegTable(m_segs);
    static_cast<ReorderTable *>(m_segs)->onMove = [this](int from, int to) {
        moveTableRow(m_segs, from, to);
        markDirty();
    };
    lay->addWidget(m_segs, 3);

    // ---- 系列协议（变体表 + 变体段表） ----
    m_seriesBox = new QGroupBox(QStringLiteral("系列协议"), page);
    auto *seriesLayout = new QVBoxLayout(m_seriesBox);
    m_seriesOn = new QCheckBox(QStringLiteral("启用"), m_seriesBox);
    seriesLayout->addWidget(m_seriesOn);

    m_seriesBody = new QWidget(m_seriesBox);
    auto *bodyLay = new QVBoxLayout(m_seriesBody);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    auto *seriesTop = new QHBoxLayout;
    m_selector = new QLineEdit(m_seriesBody);
    m_selector->setPlaceholderText(QStringLiteral("selector，如 msgid"));
    m_payload = new QLineEdit(m_seriesBody);
    m_payload->setPlaceholderText(QStringLiteral("payload 段名"));
    m_fallback = new QComboBox(m_seriesBody);
    m_fallback->addItems({QStringLiteral("reject"), QStringLiteral("unknown")});
    seriesTop->addWidget(new QLabel(QStringLiteral("selector"), m_seriesBody));
    seriesTop->addWidget(m_selector, 1);
    seriesTop->addWidget(new QLabel(QStringLiteral("payload"), m_seriesBody));
    seriesTop->addWidget(m_payload);
    seriesTop->addWidget(new QLabel(QStringLiteral("fallback"), m_seriesBody));
    seriesTop->addWidget(m_fallback);
    bodyLay->addLayout(seriesTop);

    auto *varTop = new QHBoxLayout;
    auto *varAdd = new QPushButton(QStringLiteral("+ 变体"), m_seriesBody);
    auto *varDel = new QPushButton(QStringLiteral("− 变体"), m_seriesBody);
    varAdd->setMaximumWidth(84);
    varDel->setMaximumWidth(84);
    m_variants = new QTableWidget(0, 2, m_seriesBody);
    m_variants->setHorizontalHeaderLabels({QStringLiteral("取值"), QStringLiteral("变体名")});
    m_variants->setMinimumWidth(230);
    m_variants->setMaximumWidth(270);
    m_variants->setMinimumHeight(110);
    m_variants->setMaximumHeight(110);
    m_variants->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_variants->setSelectionMode(QAbstractItemView::SingleSelection);
    varTop->addWidget(varAdd);
    varTop->addWidget(varDel);
    varTop->addWidget(m_variants);
    varTop->addWidget(new QLabel(QStringLiteral("变体段"), m_seriesBody));
    varTop->addStretch(1);
    bodyLay->addLayout(varTop);

    auto *varSegTable = new ReorderTable(0, SegColCount, m_seriesBody);
    m_varSegs = varSegTable;
    setupSegTable(m_varSegs);
    m_varSegs->setMinimumHeight(140);
    static_cast<ReorderTable *>(m_varSegs)->onMove = [this](int from, int to) {
        moveTableRow(m_varSegs, from, to);
        storeVariantSegs();
        markDirty();
    };
    bodyLay->addWidget(m_varSegs, 1);
    seriesLayout->addWidget(m_seriesBody, 1);
    auto syncSeriesBody = [this](bool on) {
        m_seriesBody->setVisible(on);
        m_seriesBox->setMaximumHeight(on ? QWIDGETSIZE_MAX : 72);
    };
    connect(m_seriesOn, &QCheckBox::toggled, this, syncSeriesBody);
    syncSeriesBody(m_seriesOn->isChecked());
    lay->addWidget(m_seriesBox, 2);

    connect(segAdd, &QPushButton::clicked, this, [this] { addSegRow(m_segs); });
    connect(segDel, &QPushButton::clicked, this, [this] { removeSegRow(m_segs); });
    connect(varAdd, &QPushButton::clicked, this, &ProtocolEditorDialog::addVariantRow);
    connect(varDel, &QPushButton::clicked, this, &ProtocolEditorDialog::removeVariantRow);
    connect(m_variants, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (m_syncing || row == m_variantRow)
            return;
        storeVariantSegs();
        m_variantRow = row;
        reloadVariantSegs();
    });
    connect(m_segs, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) { markDirty(); });
    connect(m_varSegs, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        storeVariantSegs();
        markDirty();
    });
    // 双击段表任意一行 → 段详情（"其它键"的入口）
    connect(m_segs, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        openSegmentDetail(m_segs, row);
    });
    connect(m_varSegs, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        openSegmentDetail(m_varSegs, row);
    });
}

// ---------------------------------------------------------------- 字面量

QString ProtocolEditorDialog::literalText(const QJsonValue &v)
{
    if (v.isDouble())
    {
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isString())
        return v.toString();
    return QString();
}

QJsonValue ProtocolEditorDialog::parseLiteral(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return QJsonValue(QJsonValue::Undefined);
    if (t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
        return QJsonValue(true);
    if (t.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
        return QJsonValue(false);
    bool ok = false;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    {
        const auto n = t.mid(2).toLongLong(&ok, 16);
        if (ok)
            return QJsonValue(static_cast<double>(n));
    }
    const auto n = t.toLongLong(&ok, 10);
    if (ok)
        return QJsonValue(static_cast<double>(n));
    const double d = t.toDouble(&ok);
    if (ok)
        return QJsonValue(d);
    return QJsonValue(t);
}

// ---------------------------------------------------------------- 段表填充/读取

void ProtocolEditorDialog::fillSegTable(QTableWidget *table, const QJsonArray &segs)
{
    m_syncing = true;
    table->setRowCount(0);
    for (const QJsonValue &v : segs)
    {
        const QJsonObject seg = v.toObject();
        const int r = table->rowCount();
        table->insertRow(r);

        auto *fieldItem = new QTableWidgetItem(seg.value(QStringLiteral("name")).toString());
        storeObjectOnItem(fieldItem, seg);
        table->setItem(r, SegField, fieldItem);
        table->setItem(r, SegCount,
                       new QTableWidgetItem(literalText(seg.value(QStringLiteral("count")))));
        table->setItem(r, SegType,
                       new QTableWidgetItem(seg.value(QStringLiteral("type")).toString()));
        table->setItem(r, SegValue,
                       new QTableWidgetItem(literalText(seg.value(QStringLiteral("expect")))));
        auto *other = new QTableWidgetItem(otherKeysText(seg));
        other->setFlags(other->flags() & ~Qt::ItemIsEditable);
        table->setItem(r, SegOther, other);
    }
    m_syncing = false;
}

QJsonArray ProtocolEditorDialog::segTableToJson(QTableWidget *table) const
{
    QJsonArray arr;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        auto *fieldItem = table->item(r, SegField);
        const QString name = fieldItem ? fieldItem->text().trimmed() : QString();
        if (name.isEmpty())
            continue;

        // 以原段对象为底，保住表格没覆盖的键（bit / transform / digest / scale / unit / …）。
        QJsonObject seg = objectFromItem(fieldItem);
        seg.insert(QStringLiteral("name"), name);

        const QString type =
            table->item(r, SegType) ? table->item(r, SegType)->text().trimmed() : QString();
        seg.insert(QStringLiteral("type"), type.isEmpty() ? QStringLiteral("u8") : type);

        const QJsonValue count = parseLiteral(
            table->item(r, SegCount) ? table->item(r, SegCount)->text() : QString());
        if (count.isUndefined())
            seg.remove(QStringLiteral("count"));
        else
            seg.insert(QStringLiteral("count"), count);

        const QJsonValue expect = parseLiteral(
            table->item(r, SegValue) ? table->item(r, SegValue)->text() : QString());
        if (expect.isUndefined())
            seg.remove(QStringLiteral("expect"));
        else
            seg.insert(QStringLiteral("expect"), expect);

        arr.append(seg);
    }
    return arr;
}

void ProtocolEditorDialog::fillVariantsTable(const QJsonArray &variants)
{
    m_syncing = true;
    m_variants->setRowCount(0);
    for (const QJsonValue &v : variants)
    {
        const QJsonObject var = v.toObject();
        const int r = m_variants->rowCount();
        m_variants->insertRow(r);

        // 匹配值：value 单值或 values 数组，逗号分隔。
        QString match;
        if (var.contains(QStringLiteral("values")))
        {
            QStringList parts;
            for (const QJsonValue &x : var.value(QStringLiteral("values")).toArray())
                parts << literalText(x);
            match = parts.join(QStringLiteral(","));
        }
        else if (var.contains(QStringLiteral("value")))
        {
            match = literalText(var.value(QStringLiteral("value")));
        }
        m_variants->setItem(r, 0, new QTableWidgetItem(match));

        auto *nameItem = new QTableWidgetItem(var.value(QStringLiteral("name")).toString());
        storeObjectOnItem(nameItem, var);
        m_variants->setItem(r, 1, nameItem);
    }
    m_variantRow = -1;
    m_syncing = false;
}

void ProtocolEditorDialog::reloadVariantSegs()
{
    if (m_variantRow < 0)
    {
        m_syncing = true;
        m_varSegs->setRowCount(0);
        m_syncing = false;
        return;
    }
    const QJsonObject var = objectFromItem(m_variants->item(m_variantRow, 1));
    fillSegTable(m_varSegs, var.value(QStringLiteral("segments")).toArray());
}

void ProtocolEditorDialog::storeVariantSegs()
{
    if (m_variantRow < 0 || m_variantRow >= m_variants->rowCount())
        return;
    QJsonObject var = objectFromItem(m_variants->item(m_variantRow, 1));
    var.insert(QStringLiteral("segments"), segTableToJson(m_varSegs));
    storeObjectOnItem(m_variants->item(m_variantRow, 1), var);
}

// ---------------------------------------------------------------- 变体增删

void ProtocolEditorDialog::addVariantRow()
{
    storeVariantSegs();
    const int r = m_variants->rowCount();
    m_syncing = true;
    m_variants->insertRow(r);
    m_variants->setItem(r, 0, new QTableWidgetItem(QStringLiteral("0")));
    const QString name = QStringLiteral("variant%1").arg(r + 1);
    auto *nameItem = new QTableWidgetItem(name);
    storeObjectOnItem(nameItem, QJsonObject{{QStringLiteral("name"), name}});
    m_variants->setItem(r, 1, nameItem);
    m_syncing = false;

    m_variantRow = r;
    m_variants->setCurrentCell(r, 0);
    m_syncing = true;
    m_varSegs->setRowCount(0);
    m_syncing = false;
    markDirty();
}

void ProtocolEditorDialog::removeVariantRow()
{
    const int r = m_variants->currentRow();
    if (r < 0)
        return;
    m_syncing = true;
    m_variants->removeRow(r);
    m_syncing = false;
    m_variantRow = -1;
    reloadVariantSegs();
    if (m_variants->rowCount() > 0)
        m_variants->setCurrentCell(std::min(r, m_variants->rowCount() - 1), 0);
    markDirty();
}

// ---------------------------------------------------------------- 段增删与详情

void ProtocolEditorDialog::addSegRow(QTableWidget *table)
{
    const int r = table->rowCount();
    table->insertRow(r);
    table->setItem(r, SegField, new QTableWidgetItem(QStringLiteral("field%1").arg(r + 1)));
    table->setItem(r, SegCount, new QTableWidgetItem(QStringLiteral("1")));
    table->setItem(r, SegType, new QTableWidgetItem(QStringLiteral("u8")));
    table->setItem(r, SegValue, new QTableWidgetItem(QString()));
    auto *other = new QTableWidgetItem(QString());
    other->setFlags(other->flags() & ~Qt::ItemIsEditable);
    table->setItem(r, SegOther, other);
    table->setCurrentCell(r, SegField);
    if (table == m_varSegs)
        storeVariantSegs();
    markDirty();
}

void ProtocolEditorDialog::removeSegRow(QTableWidget *table)
{
    const int r = table->currentRow();
    if (r < 0)
        return;
    m_syncing = true;
    table->removeRow(r);
    m_syncing = false;
    if (table == m_varSegs)
        storeVariantSegs();
    markDirty();
}

void ProtocolEditorDialog::openSegmentDetail(QTableWidget *table, int row)
{
    if (row < 0)
        return;
    // 详情以"表格当前值 + 原始对象"合并后的完整段为底，改完直接写回该行：
    // scale / bit / digest 这些表格外键既能看也能改，而不用在表里加列。
    QJsonObject seg = objectFromItem(table->item(row, SegField));
    auto textOf = [table, row](int col) {
        const auto *it = table->item(row, col);
        return it ? it->text().trimmed() : QString();
    };
    const QString name = textOf(SegField);
    if (!name.isEmpty())
        seg.insert(QStringLiteral("name"), name);
    const QString type = textOf(SegType);
    if (!type.isEmpty())
        seg.insert(QStringLiteral("type"), type);
    const QJsonValue count = parseLiteral(textOf(SegCount));
    if (!count.isUndefined())
        seg.insert(QStringLiteral("count"), count);
    const QJsonValue expect = parseLiteral(textOf(SegValue));
    if (!expect.isUndefined())
        seg.insert(QStringLiteral("expect"), expect);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("段详情 · %1").arg(name));
    dlg.resize(600, 500);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(
        QStringLiteral("该段的完整定义（bit / transform / digest / enum / range / length / unit 等键在这里改）")));
    auto *edit = new QPlainTextEdit;
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setPlainText(QString::fromUtf8(QJsonDocument(seg).toJson(QJsonDocument::Indented)));
    lay->addWidget(edit, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(edit->toPlainText().toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
    {
        QMessageBox::warning(this, QStringLiteral("段详情"),
                             QStringLiteral("JSON 解析失败：%1").arg(perr.errorString()));
        return;
    }
    const QJsonObject obj = d.object();
    m_syncing = true;
    auto *fieldItem = table->item(row, SegField);
    fieldItem->setText(obj.value(QStringLiteral("name")).toString());
    storeObjectOnItem(fieldItem, obj);
    table->item(row, SegCount)->setText(literalText(obj.value(QStringLiteral("count"))));
    table->item(row, SegType)->setText(obj.value(QStringLiteral("type")).toString());
    table->item(row, SegValue)->setText(literalText(obj.value(QStringLiteral("expect"))));
    table->item(row, SegOther)->setText(otherKeysText(obj));
    m_syncing = false;
    if (table == m_varSegs)
        storeVariantSegs();
    markDirty();
}

// ---------------------------------------------------------------- 同步

void ProtocolEditorDialog::markDirty()
{
    if (m_syncing)
        return;
    m_dirty = true;
    m_status->setText(QStringLiteral("未落表（点校验 / 试算 / 保存或切到 JSON 页时写入）"));
}

void ProtocolEditorDialog::refreshFromJson()
{
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
    {
        m_status->setText(QStringLiteral("JSON 解析失败，协议页暂不可信：%1").arg(perr.errorString()));
        return;
    }
    const QJsonObject doc = d.object();
    m_syncing = true;

    m_name->setText(doc.value(QStringLiteral("name")).toString());
    m_desc->setText(doc.value(QStringLiteral("description")).toString());
    m_mode->setCurrentText(doc.value(QStringLiteral("mode")).toString());
    m_layout->setCurrentText(doc.value(QStringLiteral("layout")).toString());
    m_maxsize->setValue(doc.value(QStringLiteral("maxsize")).toInt(0));
    m_endian->setCurrentText(doc.value(QStringLiteral("endian")).toString());
    const QJsonObject ts = doc.value(QStringLiteral("timestamp")).toObject();
    m_tsExpr->setText(ts.value(QStringLiteral("expr")).toString());
    m_tsUnit->setCurrentText(ts.value(QStringLiteral("unit")).toString());

    const QJsonObject series = doc.value(QStringLiteral("series")).toObject();
    m_seriesOn->setChecked(!series.isEmpty());
    m_seriesBody->setVisible(!series.isEmpty());
    m_seriesBox->setMaximumHeight(series.isEmpty() ? 72 : QWIDGETSIZE_MAX);
    m_selector->setText(series.value(QStringLiteral("selector")).toString());
    m_payload->setText(series.value(QStringLiteral("payload")).toString());
    m_fallback->setCurrentText(series.value(QStringLiteral("fallback"))
                                   .toString(QStringLiteral("reject")));

    fillSegTable(m_segs, doc.value(QStringLiteral("segments")).toArray());
    fillVariantsTable(series.value(QStringLiteral("variants")).toArray());

    m_syncing = false;
    m_dirty = false;
    if (!series.isEmpty() && m_variants->rowCount() > 0)
        m_variants->setCurrentCell(0, 0);  // 触发 reloadVariantSegs
    else
        reloadVariantSegs();
}

void ProtocolEditorDialog::commitDesign()
{
    if (!m_dirty && m_tabs && m_tabs->currentIndex() != 0)
        return;  // 用户正在 JSON 页手改、表也没脏：不拿表去覆盖
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
        return;  // JSON 本身有语法错：留给校验去报，不偷偷重建
    m_json->setPlainText(
        QString::fromUtf8(QJsonDocument(collectDoc()).toJson(QJsonDocument::Indented)));
    m_dirty = false;
}

QJsonObject ProtocolEditorDialog::collectDoc() const
{
    QJsonParseError perr;
    QJsonObject doc = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &perr).object();
    if (perr.error != QJsonParseError::NoError)
        doc = QJsonObject{};

    doc.insert(QStringLiteral("name"), m_name->text().trimmed());
    const QString desc = m_desc->text().trimmed();
    if (desc.isEmpty())
        doc.remove(QStringLiteral("description"));
    else
        doc.insert(QStringLiteral("description"), desc);
    doc.insert(QStringLiteral("mode"), m_mode->currentText());
    doc.insert(QStringLiteral("layout"), m_layout->currentText());
    if (m_maxsize->value() > 0)
        doc.insert(QStringLiteral("maxsize"), m_maxsize->value());
    else
        doc.remove(QStringLiteral("maxsize"));
    doc.insert(QStringLiteral("endian"), m_endian->currentText());

    const QString tsExpr = m_tsExpr->text().trimmed();
    if (tsExpr.isEmpty())
    {
        doc.remove(QStringLiteral("timestamp"));
    }
    else
    {
        QJsonObject ts = doc.value(QStringLiteral("timestamp")).toObject();
        ts.insert(QStringLiteral("expr"), tsExpr);
        const QString unit = m_tsUnit->currentText().trimmed();
        if (unit.isEmpty())
            ts.remove(QStringLiteral("unit"));
        else
            ts.insert(QStringLiteral("unit"), unit);
        doc.insert(QStringLiteral("timestamp"), ts);
    }

    doc.insert(QStringLiteral("segments"), segTableToJson(m_segs));

    if (m_seriesOn->isChecked())
    {
        QJsonArray variants;
        for (int r = 0; r < m_variants->rowCount(); ++r)
        {
            QJsonObject var = objectFromItem(m_variants->item(r, 1));
            const QString name =
                m_variants->item(r, 1) ? m_variants->item(r, 1)->text().trimmed() : QString();
            if (!name.isEmpty())
                var.insert(QStringLiteral("name"), name);

            const QString matchText =
                m_variants->item(r, 0) ? m_variants->item(r, 0)->text().trimmed() : QString();
            var.remove(QStringLiteral("value"));
            var.remove(QStringLiteral("values"));
            const QStringList parts = matchText.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (parts.size() > 1)
            {
                QJsonArray vals;
                for (const QString &p : parts)
                    vals.append(parseLiteral(p));
                var.insert(QStringLiteral("values"), vals);
            }
            else if (!parts.isEmpty())
            {
                const QJsonValue v = parseLiteral(parts.first());
                if (!v.isUndefined())
                    var.insert(QStringLiteral("value"), v);
            }

            // 正在编辑的那一行，段表就是它的 segments（其余行用各自缓存）。
            if (r == m_variantRow)
                var.insert(QStringLiteral("segments"), segTableToJson(m_varSegs));
            variants.append(var);
        }
        QJsonObject series;
        series.insert(QStringLiteral("selector"), m_selector->text().trimmed());
        if (!m_payload->text().trimmed().isEmpty())
            series.insert(QStringLiteral("payload"), m_payload->text().trimmed());
        series.insert(QStringLiteral("fallback"), m_fallback->currentText());
        series.insert(QStringLiteral("variants"), variants);
        doc.insert(QStringLiteral("series"), series);
    }
    else
    {
        doc.remove(QStringLiteral("series"));
    }
    return doc;
}

// ---------------------------------------------------------------- 模板文本

QString ProtocolEditorDialog::blankTemplate()
{
    // v2 形态：mode 必填、count 是元素个数、不写已废弃的 size/role/checks。
    QJsonObject o;
    o.insert(QStringLiteral("name"), QStringLiteral("新协议"));
    o.insert(QStringLiteral("mode"), QStringLiteral("bin"));
    o.insert(QStringLiteral("layout"), QStringLiteral("sequential"));
    o.insert(QStringLiteral("endian"), QStringLiteral("little"));
    o.insert(QStringLiteral("maxsize"), 256);
    QJsonArray segs;
    auto seg = [](const QString &name, const QString &type, int count) {
        QJsonObject s;
        s.insert(QStringLiteral("name"), name);
        s.insert(QStringLiteral("type"), type);
        s.insert(QStringLiteral("count"), count);
        return s;
    };
    segs.append(seg(QStringLiteral("sync"), QStringLiteral("u8"), 1));
    segs.append(seg(QStringLiteral("a"), QStringLiteral("u16"), 1));
    segs.append(seg(QStringLiteral("b"), QStringLiteral("u16"), 1));
    o.insert(QStringLiteral("segments"), segs);
    o.insert(QStringLiteral("description"), QString());
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void ProtocolEditorDialog::setTemplateText(const QString &text, const QString &path)
{
    QJsonParseError perr;
    const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8(), &perr);
    m_json->setPlainText(perr.error == QJsonParseError::NoError
                             ? QString::fromUtf8(d.toJson(QJsonDocument::Indented))
                             : text);
    if (!path.isEmpty())
        m_path = path;
    refreshFromJson();
}

void ProtocolEditorDialog::setSampleFrame(const QString &hex)
{
    m_hex->setPlainText(hex);
}

// ---------------------------------------------------------------- 文件动作

void ProtocolEditorDialog::newDoc()
{
    m_path.clear();
    m_json->setPlainText(blankTemplate());
    m_status->setText(QStringLiteral("新建协议（未保存）"));
    m_validation->setText(QStringLiteral("尚未校验"));
    m_preview->clear();
    refreshFromJson();
}

void ProtocolEditorDialog::openDoc()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开协议"), nextpilot::plotscope::host::ProtocolTemplateStore::directory(),
        QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QByteArray content;
    QString err;
    // 允许 .json 草稿，但仍受大小上限约束（8 MiB）——打开动作不该把数据文件当模板吞进去。
    if (!nextpilot::plotscope::host::ProtocolTemplateStore::read(path, &content, &err, false))
    {
        QMessageBox::warning(this, QStringLiteral("打开协议"), err);
        return;
    }
    setTemplateText(QString::fromUtf8(content), path);
    m_status->setText(QStringLiteral("已打开：%1").arg(QFileInfo(path).fileName()));
    validateDoc();
}

void ProtocolEditorDialog::saveDoc()
{
    commitDesign();
    QString path = m_path;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存协议"),
            nextpilot::plotscope::host::ProtocolTemplateStore::directory() + QStringLiteral("/new.psproto"),
            QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
        if (path.isEmpty())
            return;
    }
    QString err;
    // 保存前先过校验：不能让一份编译不了的模板落盘，之后被解析器当配置读进来。
    if (!nextpilot::plotscope::host::ProtocolTemplateStore::saveToPath(
            path, m_json->toPlainText().toUtf8(), &err))
    {
        QMessageBox::warning(this, QStringLiteral("保存协议"), err);
        m_validation->setText(err);
        return;
    }
    m_path = path;
    m_status->setText(QStringLiteral("已保存：%1").arg(path));
}

void ProtocolEditorDialog::validateDoc()
{
    commitDesign();
    const auto r = runOp(QStringLiteral("protocol.validate"),
                         {{QStringLiteral("template"), m_json->toPlainText()}});
    if (!r.ok())
    {
        m_validation->setText(QStringLiteral("校验失败：%1").arg(r.error().message));
        return;
    }
    const QVariantMap p = r.value().payload.toMap();
    const QStringList issues = p.value(QStringLiteral("issues")).toStringList();
    if (p.value(QStringLiteral("ok")).toBool())
    {
        m_validation->setText(QStringLiteral("✓ 校验通过（%1 段，%2）")
                                  .arg(p.value(QStringLiteral("segmentCount")).toInt())
                                  .arg(p.value(QStringLiteral("mode")).toString()));
        return;
    }
    // 带阶段：改模板的人要先知道卡在 JSON / 结构 / 解析 / 编译哪一层。
    m_validation->setText(QStringLiteral("校验失败［%1］共 %2 项：\n%3")
                              .arg(p.value(QStringLiteral("stage")).toString())
                              .arg(issues.size())
                              .arg(issues.join(QStringLiteral("\n"))));
}

void ProtocolEditorDialog::previewFrame()
{
    commitDesign();
    m_preview->clear();
    const QString hex = m_hex->toPlainText().trimmed();
    if (hex.isEmpty())
    {
        m_status->setText(QStringLiteral("样例帧为空"));
        return;
    }
    const auto r = runOp(QStringLiteral("protocol.preview"),
                         {{QStringLiteral("template"), m_json->toPlainText()},
                          {QStringLiteral("frame"), hex}});
    if (!r.ok())
    {
        m_status->setText(QStringLiteral("试算失败：%1").arg(r.error().message));
        return;
    }
    const QVariantMap p = r.value().payload.toMap();
    if (!p.value(QStringLiteral("ok")).toBool())
    {
        m_status->setText(QStringLiteral("试算失败：%1").arg(p.value(QStringLiteral("error")).toString()));
        return;
    }
    const QStringList lines = p.value(QStringLiteral("lines")).toStringList();
    for (const QString &line : lines)
    {
        const int colon = line.indexOf(QLatin1Char(':'));
        auto *item = new QTreeWidgetItem(m_preview);
        item->setToolTip(0, line);  // 列宽不够时悬停可看全文
        if (colon > 0)
        {
            item->setText(0, line.left(colon).trimmed());
            item->setText(1, line.mid(colon + 1).trimmed());
        }
        else
        {
            item->setText(0, line);
            item->setFirstColumnSpanned(true); // 整行跨列，避免长行被窄列截断
        }
    }
    m_status->setText(QStringLiteral("试算完成：%1 项（%2 B）")
                          .arg(lines.size())
                          .arg(p.value(QStringLiteral("frameBytes")).toInt()));
}

void ProtocolEditorDialog::applyTheme()
{
    // 走全局 QSS/palette（ThemeManager 已设），不写死色值。
    setStyleSheet(QString());
}

void ProtocolEditorDialog::configureChecksumTransform()
{
    if (!m_appCtx)
    {
        QMessageBox::information(this, QStringLiteral("Checksum 变换"),
                                 QStringLiteral("当前没有应用上下文，无法调用在线调试。"));
        return;
    }
    // 按 CapChecksum 过滤，用户只能选适用于 checksum 的算子/表达式/Lua。
    const QJsonObject req = TransformEditorDialog::run(
        this, *m_appCtx, CapChecksum, QStringLiteral("协议 Checksum"));
    if (req.isEmpty())
        return;
    m_checksumRequest = req;
    const QString id = req.value(QStringLiteral("descriptorId")).toString();
    m_checksumLabel = TransformRegistry::get(id).label;
    m_status->setText(QStringLiteral("Checksum 变换已配置：%1").arg(m_checksumLabel));
}

void ProtocolEditorDialog::run(QWidget *parent, AppContext *appCtx,
                               nextpilot::plotscope::core::IIntentExecutor *executor)
{
    ProtocolEditorDialog dlg(parent, appCtx, executor);
    dlg.exec();
}

nextpilot::plotscope::core::Result<nextpilot::plotscope::core::IntentResult>
ProtocolEditorDialog::runOp(const QString &op, const QVariantMap &params)
{
    nextpilot::plotscope::core::Intent intent;
    intent.op = op;
    intent.params = params;
    if (m_executor)
        return m_executor->execute(intent);
    // 没有宿主执行器（独立打开/截图场景）：用同一模块的 handler 直连，
    // 逻辑与模块注册的那份完全相同，不复制任何校验规则。
    static nextpilot::plotscope::host::ProtocolIntentHandler local;
    return local.execute(intent);
}

} // namespace nextpilot::plotscope::gui
