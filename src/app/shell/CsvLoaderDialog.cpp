#include "CsvLoaderDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QPushButton>

#include "ps_kernel.h"

namespace {

QLabel *makeLabel(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("color:#8b949e;font-size:11px;"));
    return l;
}

QString kindOf(const QJsonObject &c)
{
    return c.value(QStringLiteral("kind")).toString();
}

} // namespace

CsvLoaderDialog::CsvLoaderDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QStringLiteral("加载 CSV · %1").arg(QFileInfo(path).fileName()));
    setMinimumSize(900, 480);
    resize(980, 500);
    // 主窗是深色，对话框必须自己带一套深色样式，否则标签是深蓝黑、几乎看不见
    setStyleSheet(QStringLiteral(
        "QDialog{background:#0f1419;}"
        "QLabel{color:#c9d1d9;font-size:11.5px;}"
        "QGroupBox{color:#8b949e;border:1px solid #222b33;margin-top:9px;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:9px;padding:0 4px;}"
        "QComboBox,QSpinBox{background:#0b0f13;border:1px solid #2d3841;color:#c9d1d9;"
        "padding:2px 6px;font-size:11.5px;min-height:20px;}"
        "QComboBox QAbstractItemView{background:#0f1419;color:#c9d1d9;"
        "selection-background-color:#1b232a;border:1px solid #2d3841;}"
        "QCheckBox{color:#c9d1d9;font-size:11.5px;}"
        "QPushButton{background:#1b232a;border:1px solid #2d3841;color:#c9d1d9;"
        "padding:4px 16px;font-size:11.5px;}"
        "QPushButton:hover{border-color:#f2b134;color:#f2b134;}"
        "QPushButton:disabled{color:#4a545d;border-color:#222b33;}"
        "QTableWidget,QAbstractScrollArea{background:#0f1419;}"
        "QHeaderView{background:#161b22;}"
        "QTableWidget QTableCornerButton::section{background:#161b22;border:0;}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    // ---- 选项区
    auto *opt = new QGroupBox(QStringLiteral("解析选项"));
    auto *form = new QFormLayout(opt);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);

    m_delim = new QComboBox;
    m_delim->addItem(QStringLiteral("自动探测"), 0);
    m_delim->addItem(QStringLiteral("逗号  ,"), ',');
    m_delim->addItem(QStringLiteral("分号  ;"), ';');
    m_delim->addItem(QStringLiteral("制表符  \\t"), '\t');
    m_delim->addItem(QStringLiteral("空格"), ' ');
    m_delim->setFixedWidth(140);
    form->addRow(QStringLiteral("分隔符"), m_delim);

    auto *row1 = new QHBoxLayout;
    m_header = new QCheckBox(QStringLiteral("第一行是表头（title）"));
    m_header->setChecked(true);
    row1->addWidget(m_header);
    row1->addSpacing(16);
    row1->addWidget(makeLabel(QStringLiteral("跳过开头行")));
    m_skip = new QSpinBox;
    m_skip->setRange(0, 1000);
    m_skip->setFixedWidth(64);
    row1->addWidget(m_skip);
    row1->addStretch();
    form->addRow(QString(), row1);

    m_mode = new QComboBox;
    m_mode->setFixedWidth(220);
    form->addRow(QStringLiteral("时间轴"), m_mode);

    m_timeCol = new QComboBox;
    m_timeLbl = makeLabel(QStringLiteral("时间列"));
    m_timeCol->setMinimumWidth(220);
    form->addRow(m_timeLbl, m_timeCol);

    m_dateCol = new QComboBox;
    m_dateLbl = makeLabel(QStringLiteral("日期列"));
    m_dateCol->setMinimumWidth(220);
    form->addRow(m_dateLbl, m_dateCol);

    m_timeOfDay = new QComboBox;
    m_todLbl = makeLabel(QStringLiteral("时间列（时:分:秒）"));
    m_timeOfDay->setMinimumWidth(220);
    form->addRow(m_todLbl, m_timeOfDay);

    root->addWidget(opt);

    // ---- 预览 + 警告
    m_summary = new QLabel;
    m_summary->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:11.5px;"));
    root->addWidget(m_summary);

    m_preview = new QTableWidget;
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setAlternatingRowColors(true);
    m_preview->horizontalHeader()->setStretchLastSection(false);
    m_preview->verticalHeader()->setDefaultSectionSize(20);
    m_preview->setStyleSheet(QStringLiteral(
        "QTableWidget{background:#0f1419;alternate-background-color:#131a20;"
        "color:#c9d1d9;gridline-color:#222b33;font-family:monospace;font-size:11px;}"
        "QTableWidget QTableCornerButton::section{background:#161b22;border:0;}"
        "QHeaderView::section{background:#161b22;color:#8b949e;border:0;"
        "border-bottom:1px solid #222b33;padding:2px 6px;font-size:11px;}"));
    root->addWidget(m_preview, 0);

    m_warn = new QLabel;
    m_warn->setWordWrap(true);
    m_warn->setStyleSheet(QStringLiteral(
        "color:#e3b341;background:#221c0f;border:1px solid #3a2f16;padding:4px 8px;font-size:11px;"));
    root->addWidget(m_warn);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("加载"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    m_buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon());
    m_buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon());
    root->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_delim, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { reprobe(); });
    connect(m_header, &QCheckBox::toggled, this, [this] { updateWarnings(); });
    connect(m_skip, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { updateWarnings(); });
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateModeUi(); });

    reprobe();
}

void CsvLoaderDialog::reprobe()
{
    char err[256] = {0};
    QByteArray json(1 << 16, '\0');
    const char delim = static_cast<char>(m_delim->currentData().toInt());
    int32_t n = ps_csv_probe(m_path.toUtf8().constData(), delim, json.data(),
                             static_cast<uint32_t>(json.size()), err, sizeof(err));
    if (n < 0)
    {
        m_warn->setText(QString::fromUtf8(err));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    m_probe = QJsonDocument::fromJson(QByteArray(json.constData(), n)).object();
    if (delim == 0)
    {
        // 自动探测的结果回显到下拉框（不触发 reprobe）
        const QString d = m_probe.value(QStringLiteral("delimiter")).toString();
        const int idx = m_delim->findData(d.isEmpty() ? ',' : d.at(0).toLatin1());
        if (idx > 0)
        {
            QSignalBlocker block(m_delim);
            m_delim->setCurrentIndex(idx);
        }
    }

    m_header->setChecked(m_probe.value(QStringLiteral("has_header")).toBool(true));
    fillColumns();

    const QString d = m_probe.value(QStringLiteral("delimiter")).toString();
    QChar shown = d.isEmpty() ? QChar(',') : QChar(d.at(0));
    m_summary->setText(QStringLiteral("探测：%1 列 · 分隔符 “%2” · %3 行预览%4")
                           .arg(m_colCount)
                           .arg(shown == QLatin1Char('\t') ? QStringLiteral("\\t") : QString(shown))
                           .arg(m_probe.value(QStringLiteral("preview")).toArray().size())
                           .arg(m_probe.value(QStringLiteral("delimiter_ambiguous")).toBool()
                                    ? QStringLiteral(" · 分隔符有歧义")
                                    : QString()));
    updateWarnings();
}

void CsvLoaderDialog::fillColumns()
{
    m_timeCol->clear();
    m_dateCol->clear();
    m_timeOfDay->clear();
    m_preview->clear();

    const QJsonArray cols = m_probe.value(QStringLiteral("columns")).toArray();
    m_colCount = cols.size();

    QStringList names;
    int dateIdx = -1, todIdx = -1, suggested = -1;
    for (int i = 0; i < cols.size(); ++i)
    {
        const QJsonObject c = cols.at(i).toObject();
        const QString name = c.value(QStringLiteral("name")).toString();
        names << name;
        m_timeCol->addItem(name, i);
        if (kindOf(c) == QLatin1String("date_only"))
        {
            m_dateCol->addItem(name, i);
            if (dateIdx < 0)
                dateIdx = i;
        }
        else if (kindOf(c) == QLatin1String("time_only"))
        {
            m_timeOfDay->addItem(name, i);
            if (todIdx < 0)
                todIdx = i;
        }
    }
    if (m_probe.contains(QStringLiteral("suggested_time")))
        suggested = m_probe.value(QStringLiteral("suggested_time")).toInt();

    // 模式项：行号永远有；日期+时间只在两类列都存在时出现
    m_mode->clear();
    m_mode->addItem(QStringLiteral("行号（不解析时间）"), QStringLiteral("row"));
    m_mode->addItem(QStringLiteral("指定一列作为时间"), QStringLiteral("column"));
    if (dateIdx >= 0 && todIdx >= 0)
        m_mode->addItem(QStringLiteral("日期列 + 时间列合并"), QStringLiteral("combined"));

    // 默认落在"指定列"（若探测到疑似时间列），否则行号——不默认吃掉第一列
    if (suggested >= 0 && m_timeCol->count() > 0)
    {
        m_mode->setCurrentIndex(1);
        m_timeCol->setCurrentIndex(suggested);
    }
    m_dateCol->setCurrentIndex(m_dateCol->count() > 0 ? 0 : -1);
    m_timeOfDay->setCurrentIndex(m_timeOfDay->count() > 0 ? 0 : -1);

    // 预览表
    const QJsonArray preview = m_probe.value(QStringLiteral("preview")).toArray();
    m_preview->setColumnCount(m_colCount + 1);
    QStringList heads{QStringLiteral("#")};
    heads << names;
    m_preview->setHorizontalHeaderLabels(heads);
    m_preview->setRowCount(preview.size());
    for (int r = 0; r < preview.size(); ++r)
    {
        auto *idxItem = new QTableWidgetItem(QString::number(r));
        idxItem->setForeground(QColor(QStringLiteral("#6e7681")));
        m_preview->setItem(r, 0, idxItem);
        const QJsonArray cells = preview.at(r).toArray();
        for (int c = 0; c < m_colCount; ++c)
        {
            const QString v = c < cells.size() ? cells.at(c).toString() : QString();
            m_preview->setItem(r, c + 1, new QTableWidgetItem(v));
        }
    }
    m_preview->resizeColumnsToContents();
    m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_preview->setColumnWidth(0, 40);
    // 表格高度贴合内容，别留一大片空画板
    const int rows = qMax(1, m_preview->rowCount());
    m_preview->setFixedHeight(m_preview->horizontalHeader()->height() + rows * 20 + 4);

    updateModeUi();
}

void CsvLoaderDialog::updateModeUi()
{
    const QString mode = m_mode->currentData().toString();
    const bool isCol = mode == QLatin1String("column");
    const bool isCombined = mode == QLatin1String("combined");

    m_timeCol->setVisible(isCol);
    m_timeLbl->setVisible(isCol);
    m_dateCol->setVisible(isCombined);
    m_dateLbl->setVisible(isCombined);
    m_timeOfDay->setVisible(isCombined);
    m_todLbl->setVisible(isCombined);
}

void CsvLoaderDialog::updateWarnings()
{
    QStringList w;
    const QJsonArray arr = m_probe.value(QStringLiteral("warnings")).toArray();
    for (const auto &v : arr)
        w << v.toString();
    if (!m_probe.value(QStringLiteral("has_header")).toBool() && m_header->isChecked())
        w << QStringLiteral("探测认为首行是数据，但你勾了“第一行是表头”，请确认。");
    if (m_probe.value(QStringLiteral("has_header")).toBool() && !m_header->isChecked())
        w << QStringLiteral("探测认为首行是表头，取消勾选会把表头当数据行。");
    if (m_skip->value() > 0)
        w << QStringLiteral("将跳过前 %1 行（表头在跳过行之后）。").arg(m_skip->value());

    m_warn->setVisible(!w.isEmpty());
    m_warn->setText(w.join(QStringLiteral("  ·  ")));
}

QString CsvLoaderDialog::optionsJson() const
{
    QJsonObject o;
    int delim = m_delim->currentData().toInt();
    if (delim == 0)
        delim = m_probe.value(QStringLiteral("delimiter")).toString().at(0).toLatin1();
    o.insert(QStringLiteral("delimiter"), QString(QChar(delim)));
    o.insert(QStringLiteral("has_header"), m_header->isChecked());
    o.insert(QStringLiteral("skip_rows"), m_skip->value());

    QJsonObject t;
    const QString mode = m_mode->currentData().toString();
    if (mode == QLatin1String("column"))
    {
        t.insert(QStringLiteral("mode"), QStringLiteral("column"));
        t.insert(QStringLiteral("index"), m_timeCol->currentData().toInt());
    }
    else if (mode == QLatin1String("combined"))
    {
        t.insert(QStringLiteral("mode"), QStringLiteral("combined"));
        t.insert(QStringLiteral("date"), m_dateCol->currentData().toInt());
        t.insert(QStringLiteral("time"), m_timeOfDay->currentData().toInt());
    }
    else
    {
        t.insert(QStringLiteral("mode"), QStringLiteral("row"));
    }
    o.insert(QStringLiteral("time"), t);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
