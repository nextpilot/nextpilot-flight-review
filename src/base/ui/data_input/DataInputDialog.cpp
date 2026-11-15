#include "data_input/DataInputDialog.h"

#include "host/extension/DataInputEntry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

void setNested(QJsonObject &o, const QString &path, const QJsonValue &value)
{
    const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return;
    if (parts.size() == 1)
    {
        o.insert(parts.first(), value);
        return;
    }
    QJsonObject child = o.value(parts.first()).toObject();
    setNested(child, QStringList(parts.mid(1)).join(QLatin1Char('.')), value);
    o.insert(parts.first(), child);
}

QVariant probeVariant(const QJsonObject &probe, const QString &key)
{
    const QJsonValue v = probe.value(key);
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return static_cast<int>(v.toDouble());
    return {};
}

QString normalizeTransport(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("uart"))
        return QStringLiteral("serial");
    return v;
}

QString transportLabel(const QString &value)
{
    if (value == QLatin1String("file"))
        return QStringLiteral("文件");
    if (value == QLatin1String("udp"))
        return QStringLiteral("UDP");
    if (value == QLatin1String("tcp"))
        return QStringLiteral("TCP");
    if (value == QLatin1String("serial"))
        return QStringLiteral("UART / 串口");
    if (value == QLatin1String("internal"))
        return QStringLiteral("内置");
    return value;
}

QJsonObject defaultTransportFields(const QString &transport, const QJsonObject &defaults)
{
    QJsonObject f;
    if (transport == QLatin1String("udp"))
    {
        f.insert(QStringLiteral("bind"),
                 defaults.value(QStringLiteral("bind")).toString(QStringLiteral("127.0.0.1")));
        f.insert(QStringLiteral("port"), defaults.value(QStringLiteral("port")).toInt(1223));
    }
    else if (transport == QLatin1String("tcp"))
    {
        f.insert(QStringLiteral("host"),
                 defaults.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")));
        f.insert(QStringLiteral("port"), defaults.value(QStringLiteral("port")).toInt(3001));
        f.insert(QStringLiteral("timeout_ms"),
                 defaults.value(QStringLiteral("timeout_ms")).toInt(2000));
    }
    else if (transport == QLatin1String("serial"))
    {
        f.insert(QStringLiteral("serial_port"),
                 defaults.value(QStringLiteral("serial_port")).toString());
        f.insert(QStringLiteral("baud"), defaults.value(QStringLiteral("baud")).toInt(115200));
    }
    return f;
}

QJsonValue valueForField(const QJsonObject &field, const QJsonObject &initial,
                         const QJsonObject &defaults, const QJsonObject &values)
{
    const QString key = field.value(QStringLiteral("key")).toString();
    if (values.contains(key))
        return values.value(key);
    if (initial.contains(key))
        return initial.value(key);
    if (defaults.contains(key))
        return defaults.value(key);
    return field.value(QStringLiteral("default"));
}

} // namespace

DataInputDialog::DataInputDialog(const DataInputEntry &entry, const QString &path, QWidget *parent)
    : QDialog(parent), m_sourceMode(false), m_path(path), m_fixedPath(true)
{
    m_title = entry.schema.value(QStringLiteral("title")).toString(entry.label);
    m_subtitle = QFileInfo(path).fileName();
    m_fileFilter = entry.filter;

    ParserPane pane;
    pane.id = entry.id;
    pane.label = entry.label;
    pane.kind = entry.formatTag;
    pane.transports = QStringList{QStringLiteral("file")};
    pane.schema = entry.schema;
    pane.defaults = QJsonDocument::fromJson(
                        entry.defaultOptions ? entry.defaultOptions(path).toUtf8() : QByteArray("{}"))
                        .object();
    pane.recordable = false;
    pane.replayable = false;
    pane.probeAvailable = static_cast<bool>(entry.probe);
    pane.fileFilter = entry.filter;
    pane.probeFn = entry.probe;
    pane.editFn = entry.edit;
    m_panes.append(pane);

    m_transports = QStringList{QStringLiteral("file")};
    m_defaultTransport = QStringLiteral("file");
    m_defaultUri = QStringLiteral("file://");
    m_transportDefaults = pane.defaults;
    m_probeAvailable = pane.probeAvailable;
    m_preferredId = pane.id;

    initUi();
    refreshProbe(true);
}

DataInputDialog::DataInputDialog(const QVector<DataInputEntry> &parsers, const QString &preferredId,
                                 const QString &initialConfig, QWidget *parent)
    : QDialog(parent), m_sourceMode(true)
{
    m_initial = QJsonDocument::fromJson(initialConfig.toUtf8()).object();
    m_preferredId = preferredId;
    m_title = QStringLiteral("数据输入");

    const QJsonArray initialParsers = m_initial.value(QStringLiteral("parsers")).toArray();

    for (const DataInputEntry &entry : parsers)
    {
        ParserPane pane;
        pane.id = entry.id;
        pane.label = entry.label;
        pane.kind = entry.kind;
        pane.schema = entry.schema;
        pane.defaults = QJsonDocument::fromJson(
                            entry.defaultConfig ? entry.defaultConfig().toUtf8() : QByteArray("{}"))
                            .object();
        pane.recordable = entry.recordable;
        pane.replayable = entry.replayable;
        pane.editFn = entry.edit;
        pane.fileFilter = entry.replayExtensions.isEmpty()
                              ? QStringLiteral("全部 (*)")
                              : QStringLiteral("原始流 (%1);;全部 (*)").arg(entry.replayExtensions);

        const QJsonObject transport = pane.schema.value(QStringLiteral("transport")).toObject();
        for (const QJsonValue &v : transport.value(QStringLiteral("supported")).toArray())
        {
            const QString t = normalizeTransport(v.toString());
            if (!t.isEmpty() && !pane.transports.contains(t))
                pane.transports << t;
        }
        if (pane.transports.isEmpty())
            pane.transports = QStringList{QStringLiteral("internal")};

        bool hasInitial = false;
        for (const QJsonValue &v : initialParsers)
        {
            const QJsonObject p = v.toObject();
            if (p.value(QStringLiteral("id")).toString() != pane.id)
                continue;
            pane.initial = p.value(QStringLiteral("options")).toObject();
            hasInitial = true;
            break;
        }
        if (!hasInitial && initialParsers.isEmpty() && pane.id == m_preferredId)
            pane.initial = m_initial;

        m_panes.append(pane);
        for (const QString &t : pane.transports)
        {
            if (!m_transports.contains(t))
                m_transports << t;
        }
    }

    if (m_panes.isEmpty())
        m_panes.append(ParserPane{});

    if (m_preferredId.isEmpty() || !primaryPane())
        m_preferredId = m_panes.first().id;

    // 选中的 parse 决定初始 transport 与默认值；其余 parser 仍可在同一 transport 上勾选。
    const ParserPane *pref = primaryPane();
    if (pref)
    {
        const QJsonObject transport = pref->schema.value(QStringLiteral("transport")).toObject();
        m_defaultUri = transport.value(QStringLiteral("default")).toString();
        m_defaultTransport = normalizeTransport(m_defaultUri);
        if (m_defaultTransport.contains(QLatin1String("://")))
        {
            m_defaultTransport = m_defaultTransport.section(QLatin1String("://"), 0, 0);
            applyTransportDefaultUri(m_defaultUri);
        }
        if (m_defaultTransport.isEmpty())
            m_defaultTransport = pref->defaults.value(QStringLiteral("transport")).toString();
        if (m_defaultTransport.isEmpty())
            m_defaultTransport = m_transports.value(0);
        if (!m_defaultUri.contains(QLatin1String("://")))
            m_defaultUri.clear();
        // 当前连接入口已经确定了一个 parse 扩展，transport 以它声明的集合为准；
        // 其余 parse 只能在同一 transport 上追加，避免生成无法由入口扩展消费的配置。
        m_transports = pref->transports;
    }

    const QString initialTransport = m_initial.value(QStringLiteral("transport")).toString();
    if (!initialTransport.isEmpty())
    {
        if (initialTransport.contains(QLatin1String("://")))
            applyTransportDefaultUri(initialTransport);
        else
            m_defaultTransport = normalizeTransport(initialTransport);
    }

    for (const ParserPane &pane : m_panes)
    {
        if (pane.id == m_preferredId)
            m_transportDefaults = pane.defaults;
        m_rawCapture = m_rawCapture || pane.replayable;
        m_recordable = m_recordable || pane.recordable;
    }
    if (m_transportDefaults.isEmpty() && pref)
        m_transportDefaults = pref->defaults;

    m_fileFilter = pref ? pref->fileFilter : QStringLiteral("全部 (*)");
    m_transports.removeDuplicates();
    if (m_transports.isEmpty())
        m_transports = QStringList{QStringLiteral("file")};
    if (!m_transports.contains(m_defaultTransport))
        m_defaultTransport = m_transports.first();
    if (m_defaultTransport.isEmpty())
        m_defaultTransport = m_transports.first();

    initUi();
}

void DataInputDialog::applyTransportDefaultUri(const QString &uri)
{
    if (uri.isEmpty())
        return;
    if (!uri.contains(QLatin1String("://")))
    {
        m_defaultTransport = normalizeTransport(uri);
        return;
    }

    const QUrl url(uri);
    const QString scheme = normalizeTransport(url.scheme());
    if (scheme.isEmpty())
        return;
    m_defaultTransport = scheme;
    m_defaultUri = uri;
    if (scheme == QLatin1String("udp"))
    {
        if (!m_initial.contains(QStringLiteral("bind")) && !url.host().isEmpty())
            m_initial.insert(QStringLiteral("bind"), url.host());
        if (!m_initial.contains(QStringLiteral("port")) && url.port() > 0)
            m_initial.insert(QStringLiteral("port"), url.port());
    }
    else if (scheme == QLatin1String("tcp"))
    {
        if (!m_initial.contains(QStringLiteral("host")) && !url.host().isEmpty())
            m_initial.insert(QStringLiteral("host"), url.host());
        if (!m_initial.contains(QStringLiteral("port")) && url.port() > 0)
            m_initial.insert(QStringLiteral("port"), url.port());
    }
    else if (scheme == QLatin1String("file"))
    {
        if (!m_initial.contains(QStringLiteral("filepath")) && !url.path().isEmpty())
            m_initial.insert(QStringLiteral("filepath"), url.toLocalFile());
    }
}

void DataInputDialog::initUi()
{
    setWindowTitle(m_sourceMode ? QStringLiteral("连接 · 数据输入")
                                : QStringLiteral("加载 · %1").arg(m_title));
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#0d1117")));
    setPalette(pal);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#0d1117;color:#c9d1d9;}"
        "QLabel{color:#c9d1d9;}"
        "QGroupBox{background:#0d1117;border:1px solid #30363d;margin-top:8px;padding-top:6px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:#8b949e;}"
        "QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit,QCheckBox{background:#161b22;color:#c9d1d9;"
        "border:1px solid #30363d;padding:3px 6px;min-height:20px;}"
        "QComboBox::drop-down{border:0;}"
        "QCheckBox{padding:3px 6px;}"
        "QListWidget{background:#0d1117;color:#c9d1d9;border:1px solid #30363d;}"
        "QListWidget::item{padding:3px 6px;}"
        "QTableWidget{background:#0d1117;alternate-background-color:#141a21;color:#c9d1d9;"
        "gridline-color:#21262d;border:1px solid #30363d;}"
        "QHeaderView::section{background:#161b22;color:#8b949e;border:0;border-right:1px solid #21262d;"
        "padding:3px 5px;font-size:11px;}"
        "QPushButton{background:#21262d;color:#c9d1d9;border:1px solid #30363d;padding:4px 18px;border-radius:2px;}"
        "QPushButton:hover{background:#30363d;}"));
    setMinimumSize(460, 340);
    resize(m_sourceMode ? 540 : 980, m_sourceMode ? 480 : 540);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    auto *transportBox = new QGroupBox(QStringLiteral("1 · Transport 传输"));
    auto *transportLayout = new QFormLayout(transportBox);
    transportLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    transportLayout->setHorizontalSpacing(10);
    transportLayout->setVerticalSpacing(6);

    m_transport = new QComboBox;
    for (const QString &t : m_transports)
        m_transport->addItem(transportLabel(t), t);
    int idx = m_transport->findData(m_defaultTransport);
    m_transport->setCurrentIndex(idx >= 0 ? idx : 0);

    m_transportRow = new QWidget;
    auto *tr = new QHBoxLayout(m_transportRow);
    tr->setContentsMargins(0, 0, 0, 0);
    tr->setSpacing(8);
    tr->addWidget(new QLabel(QStringLiteral("传输协议")));
    tr->addWidget(m_transport, 1);
    transportLayout->addRow(m_transportRow);
    // 只有一种 transport 时，不渲染选择器。
    m_transportRow->setVisible(m_transports.size() > 1);

    auto *transportFields = new QWidget;
    m_transportForm = new QFormLayout(transportFields);
    m_transportForm->setContentsMargins(0, 0, 0, 0);
    m_transportForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_transportForm->setHorizontalSpacing(10);
    m_transportForm->setVerticalSpacing(6);
    transportLayout->addRow(transportFields);
    root->addWidget(transportBox);
    buildTransportFields();

    connect(m_transport, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        collectTransportValues();
        updateTransportAvailability();
        buildTransportFields();
    });

    m_parserBox = new QGroupBox(QStringLiteral("2 · Parse 解析"));
    auto *parserBoxLayout = new QVBoxLayout(m_parserBox);
    parserBoxLayout->setContentsMargins(0, 0, 0, 0);
    parserBoxLayout->setSpacing(6);

    m_parserTitle = new QLabel(QStringLiteral("解析协议"));
    m_parserTitle->setStyleSheet(QStringLiteral("color:#8b949e;padding-left:8px;"));
    parserBoxLayout->addWidget(m_parserTitle);

    m_parserList = new QListWidget;
    m_parserList->setFixedHeight(m_sourceMode ? 78 : 0);
    m_parserList->setVisible(m_sourceMode && m_panes.size() > 1);
    parserBoxLayout->addWidget(m_parserList);

    auto *parserHost = new QWidget;
    m_parserLayout = new QVBoxLayout(parserHost);
    m_parserLayout->setContentsMargins(0, 0, 0, 0);
    m_parserLayout->setSpacing(6);
    parserBoxLayout->addWidget(parserHost);
    root->addWidget(m_parserBox, 1);

    buildParserPanes();
    rebuildParserSelection();

    if (m_sourceMode && m_recordable)
    {
        m_recordRow = new QWidget;
        auto *recordLayout = new QHBoxLayout(m_recordRow);
        recordLayout->setContentsMargins(0, 0, 0, 0);
        recordLayout->setSpacing(8);
        m_record = new QCheckBox(QStringLiteral("连接后记录"));
        m_record->setChecked(m_initial.value(QStringLiteral("__record")).toBool());
        m_recordFormat = new QComboBox;
        m_recordFormat->addItem(QStringLiteral("MCAP（解析后）"), QStringLiteral("mcap"));
        m_recordFormat->addItem(QStringLiteral("RAW（原始字节）"), QStringLiteral("raw"));
        m_recordFormat->addItem(QStringLiteral("CSV（解码通道）"), QStringLiteral("csv"));
        const QString format = m_initial.value(QStringLiteral("__record_format")).toString(
            QStringLiteral("mcap"));
        const int formatIndex = m_recordFormat->findData(format);
        m_recordFormat->setCurrentIndex(formatIndex >= 0 ? formatIndex : 0);
        m_recordFormat->setEnabled(m_record->isChecked());
        connect(m_record, &QCheckBox::toggled, m_recordFormat, &QWidget::setEnabled);
        recordLayout->addWidget(m_record);
        recordLayout->addWidget(m_recordFormat, 1);
        root->addWidget(m_recordRow);
    }

    updateParserVisibility();

    m_summary = new QLabel;
    m_summary->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:11.5px;"));
    root->addWidget(m_summary);

    m_preview = new QTableWidget;
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setAlternatingRowColors(true);
    m_preview->horizontalHeader()->setStretchLastSection(false);
    m_preview->verticalHeader()->setDefaultSectionSize(20);
    root->addWidget(m_preview);

    m_warn = new QLabel;
    m_warn->setWordWrap(true);
    m_warn->setStyleSheet(QStringLiteral(
        "color:#e3b341;background:#221c0f;border:1px solid #3a2f16;padding:4px 8px;font-size:11px;"));
    root->addWidget(m_warn);

    if (!m_probeAvailable)
    {
        m_summary->setVisible(false);
        m_preview->setVisible(false);
        m_warn->setVisible(false);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(m_sourceMode ? QStringLiteral("连接")
                                                               : QStringLiteral("加载"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QWidget *DataInputDialog::createEditor(const QJsonObject &f, const QJsonValue &value,
                                       QHash<QString, QWidget *> &editors, QFormLayout *form,
                                       const std::function<QDialog *(const QString &, QWidget *)> &editFn)
{
    const QString key = f.value(QStringLiteral("key")).toString();
    const QString type = f.value(QStringLiteral("type")).toString(QStringLiteral("string"));
    const QString label = f.value(QStringLiteral("label")).toString(key);
    QWidget *editor = nullptr;

    if (type == QLatin1String("bool"))
    {
        auto *cb = new QCheckBox(f.value(QStringLiteral("hint")).toString());
        cb->setChecked(value.toBool());
        editor = cb;
    }
    else if (type == QLatin1String("int"))
    {
        auto *sp = new QSpinBox;
        sp->setRange(f.value(QStringLiteral("min")).toInt(0), f.value(QStringLiteral("max")).toInt(1000000));
        sp->setValue(value.toInt(f.value(QStringLiteral("default")).toInt()));
        if (f.contains(QStringLiteral("suffix")))
            sp->setSuffix(f.value(QStringLiteral("suffix")).toString());
        editor = sp;
    }
    else if (type == QLatin1String("double"))
    {
        auto *sp = new QDoubleSpinBox;
        sp->setRange(f.value(QStringLiteral("min")).toDouble(-1e9),
                     f.value(QStringLiteral("max")).toDouble(1e9));
        sp->setDecimals(f.value(QStringLiteral("decimals")).toInt(3));
        sp->setValue(value.toDouble(f.value(QStringLiteral("default")).toDouble()));
        editor = sp;
    }
    else if (type == QLatin1String("enum"))
    {
        auto *cb = new QComboBox;
        for (const QJsonValue &ov : f.value(QStringLiteral("options")).toArray())
        {
            const QJsonObject o = ov.toObject();
            cb->addItem(o.value(QStringLiteral("label")).toString(),
                        o.value(QStringLiteral("value")).toString());
        }
        const int idx = cb->findData(value.toString());
        if (idx >= 0)
            cb->setCurrentIndex(idx);
        editor = cb;
    }
    else if (type == QLatin1String("file"))
    {
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        auto *line = new QLineEdit(value.toString(), row);
        line->setPlaceholderText(f.value(QStringLiteral("placeholder")).toString());
        auto *browse = new QPushButton(QStringLiteral("浏览…"), row);
        h->addWidget(line, 1);
        h->addWidget(browse);
        QPushButton *edit = nullptr;
        if (key == QLatin1String("protocol"))
        {
            edit = new QPushButton(QStringLiteral("编辑…"), row);
            h->addWidget(edit);
        }
        if (m_fixedPath && key == QLatin1String("filepath"))
        {
            line->setText(m_path);
            line->setReadOnly(true);
            browse->setVisible(false);
        }
        if (edit)
        {
            connect(edit, &QPushButton::clicked, this, [this, line, editFn] {
                const QString path = line->text().trimmed();
                if (path.isEmpty())
                {
                    QMessageBox::information(this, QStringLiteral("协议模板"),
                                             QStringLiteral("先选择一个 .psproto 模板文件"));
                    return;
                }
                QDialog *dlg = editFn ? editFn(path, this) : nullptr;
                if (!dlg)
                {
                    QMessageBox::information(this, QStringLiteral("协议模板"),
                                             QStringLiteral("未安装自定义协议组件"));
                    return;
                }
                if (dlg->exec() == QDialog::Accepted)
                    line->setText(path);
                delete dlg;
            });
        }
        connect(browse, &QPushButton::clicked, this, [this, line, f] {
            const QString filter = f.value(QStringLiteral("filter")).toString(m_fileFilter);
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"),
                                                             QString(), filter);
            if (!path.isEmpty())
                line->setText(path);
        });
        editors.insert(key, line);
        form->addRow(label, row);
        return row;
    }
    else
    {
        auto *line = new QLineEdit(value.toString());
        line->setPlaceholderText(f.value(QStringLiteral("placeholder")).toString());
        editor = line;
    }

    if (editor)
    {
        editors.insert(key, editor);
        form->addRow(label, editor);
    }
    return editor;
}

QString DataInputDialog::currentTransportType() const
{
    if (m_transport)
        return normalizeTransport(m_transport->currentData().toString());
    return m_defaultTransport;
}

void DataInputDialog::collectTransportValues()
{
    for (auto it = m_transportEditors.constBegin(); it != m_transportEditors.constEnd(); ++it)
    {
        QWidget *w = it.value();
        if (auto *cb = qobject_cast<QCheckBox *>(w))
            m_transportValues.insert(it.key(), cb->isChecked());
        else if (auto *sp = qobject_cast<QSpinBox *>(w))
            m_transportValues.insert(it.key(), sp->value());
        else if (auto *dsp = qobject_cast<QDoubleSpinBox *>(w))
            m_transportValues.insert(it.key(), dsp->value());
        else if (auto *co = qobject_cast<QComboBox *>(w))
            m_transportValues.insert(it.key(), co->currentData().toString());
        else if (auto *line = qobject_cast<QLineEdit *>(w))
            m_transportValues.insert(it.key(), line->text());
    }
}

void DataInputDialog::buildTransportFields()
{
    if (!m_transportForm)
        return;
    while (m_transportForm->rowCount() > 0)
        m_transportForm->removeRow(0);
    if (QWidget *host = m_transportForm->parentWidget())
    {
        const auto children = host->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : children)
            delete child;
    }
    m_transportEditors.clear();

    const QString transport = currentTransportType();
    QJsonArray fields;
    auto add = [&fields](const QString &key, const QString &type, const QString &label,
                         const QJsonValue &def, int min = 0, int max = 0,
                         const QString &suffix = QString()) {
        QJsonObject f{{QStringLiteral("key"), key}, {QStringLiteral("type"), type},
                      {QStringLiteral("label"), label}, {QStringLiteral("default"), def}};
        if (min != 0 || max != 0)
        {
            f.insert(QStringLiteral("min"), min);
            f.insert(QStringLiteral("max"), max);
        }
        if (!suffix.isEmpty())
            f.insert(QStringLiteral("suffix"), suffix);
        fields.append(f);
    };
    auto def = [this, transport](const QString &key, const QJsonValue &fallback) {
        QJsonObject d = defaultTransportFields(transport, m_transportDefaults);
        if (d.contains(key))
            return d.value(key);
        return fallback;
    };

    if (transport == QLatin1String("file"))
    {
        QJsonObject f{{QStringLiteral("key"), QStringLiteral("filepath")},
                      {QStringLiteral("type"), QStringLiteral("file")},
                      {QStringLiteral("label"), QStringLiteral("文件")},
                      {QStringLiteral("filter"), m_fileFilter},
                      {QStringLiteral("default"), m_path}};
        fields.append(f);
    }
    else if (transport == QLatin1String("udp"))
    {
        QJsonObject bind{{QStringLiteral("key"), QStringLiteral("bind")},
                         {QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("label"), QStringLiteral("IP / 绑定地址")},
                         {QStringLiteral("default"), def(QStringLiteral("bind"), QStringLiteral("127.0.0.1"))}};
        fields.append(bind);
        add(QStringLiteral("port"), QStringLiteral("int"), QStringLiteral("端口"),
            def(QStringLiteral("port"), 1223), 1, 65535);
    }
    else if (transport == QLatin1String("tcp"))
    {
        QJsonObject host{{QStringLiteral("key"), QStringLiteral("host")},
                         {QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("label"), QStringLiteral("主机地址")},
                         {QStringLiteral("default"), def(QStringLiteral("host"), QStringLiteral("127.0.0.1"))}};
        fields.append(host);
        add(QStringLiteral("port"), QStringLiteral("int"), QStringLiteral("端口"),
            def(QStringLiteral("port"), 3001), 1, 65535);
        add(QStringLiteral("timeout_ms"), QStringLiteral("int"), QStringLiteral("连接超时"),
            def(QStringLiteral("timeout_ms"), 2000), 100, 10000, QStringLiteral(" ms"));
    }
    else if (transport == QLatin1String("serial"))
    {
        QJsonObject port{{QStringLiteral("key"), QStringLiteral("serial_port")},
                         {QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("label"), QStringLiteral("串口")},
                         {QStringLiteral("default"), def(QStringLiteral("serial_port"), QString())}};
        fields.append(port);
        add(QStringLiteral("baud"), QStringLiteral("int"), QStringLiteral("波特率"),
            def(QStringLiteral("baud"), 115200), 1200, 921600);
    }

    bool canReplay = false;
    const QStringList selected = selectedParserIds();
    for (const ParserPane &pane : m_panes)
    {
        if (selected.contains(pane.id))
            canReplay = canReplay || pane.replayable;
    }
    if (m_rawCapture && canReplay && transport != QLatin1String("file") &&
        transport != QLatin1String("internal"))
    {
        QJsonObject f{{QStringLiteral("key"), QStringLiteral("raw_path")},
                      {QStringLiteral("type"), QStringLiteral("string")},
                      {QStringLiteral("label"), QStringLiteral("原始字节落盘（留空不录）")},
                      {QStringLiteral("default"),
                       def(QStringLiteral("raw_path"), QString())}};
        fields.append(f);
    }

    for (const QJsonValue &fv : fields)
    {
        const QJsonObject f = fv.toObject();
        createEditor(f, valueForField(f, m_initial, m_transportDefaults, m_transportValues),
                     m_transportEditors, m_transportForm, {});
    }
}

bool DataInputDialog::parserSupportsTransport(const ParserPane &pane, const QString &transport) const
{
    if (pane.transports.isEmpty())
        return true;
    return pane.transports.contains(transport);
}

void DataInputDialog::buildParserPanes()
{
    QSignalBlocker listBlock(m_parserList);
    m_parserList->clear();

    const QString transport = currentTransportType();
    for (int i = 0; i < m_panes.size(); ++i)
    {
        ParserPane &pane = m_panes[i];
        pane.box = new QGroupBox;
        pane.form = new QFormLayout(pane.box);
        pane.form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pane.form->setHorizontalSpacing(10);
        pane.form->setVerticalSpacing(6);
        pane.form->setContentsMargins(8, 4, 8, 8);

        if (m_panes.size() > 1)
            pane.box->setTitle(QStringLiteral("Parse · %1").arg(pane.label));

        QJsonObject parser = pane.schema.value(QStringLiteral("parser")).toObject();
        QJsonArray fields = parser.value(QStringLiteral("fields")).toArray();
        if (fields.isEmpty())
            fields = pane.schema.value(QStringLiteral("fields")).toArray();

        pane.fields.clear();
        pane.editors.clear();
        for (const QJsonValue &fv : fields)
        {
            const QJsonObject f = fv.toObject();
            Field field;
            field.key = f.value(QStringLiteral("key")).toString();
            field.type = f.value(QStringLiteral("type")).toString(QStringLiteral("string"));
            field.spec = f;
            field.editor = createEditor(f, valueForField(f, pane.initial, pane.defaults, {}),
                                        pane.editors, pane.form, pane.editFn);
            if (!field.editor)
                continue;
            field.label = qobject_cast<QLabel *>(pane.form->labelForField(field.editor));
            pane.fields.append(field);

            if (field.type == QLatin1String("bool"))
            {
                connect(qobject_cast<QCheckBox *>(field.editor), &QCheckBox::toggled, this, [this, i] {
                    if (!m_updating && i < m_panes.size())
                    {
                        updateVisibility(m_panes[i]);
                        if (m_probeAvailable)
                            refreshProbe(false);
                    }
                });
            }
            else if (field.type == QLatin1String("int"))
            {
                connect(qobject_cast<QSpinBox *>(field.editor),
                        QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i] {
                            if (!m_updating && i < m_panes.size() && m_probeAvailable)
                                refreshProbe(false);
                        });
            }
            else if (field.type == QLatin1String("double"))
            {
                connect(qobject_cast<QDoubleSpinBox *>(field.editor),
                        QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, i] {
                            if (!m_updating && i < m_panes.size() && m_probeAvailable)
                                refreshProbe(false);
                        });
            }
            else if (field.type == QLatin1String("enum") || field.type == QLatin1String("string"))
            {
                if (auto *cb = qobject_cast<QComboBox *>(field.editor))
                    connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i] {
                        if (!m_updating && i < m_panes.size())
                        {
                            updateVisibility(m_panes[i]);
                            if (m_probeAvailable)
                                refreshProbe(false);
                        }
                    });
            }
        }
        m_parserLayout->addWidget(pane.box);

        if (m_sourceMode && m_panes.size() > 1)
        {
            auto *item = new QListWidgetItem(pane.label);
            item->setData(Qt::UserRole, pane.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            item->setCheckState(Qt::Unchecked);
            m_parserList->addItem(item);
        }
    }

    if (m_parserList)
    {
        connect(m_parserList, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
            if (m_updating)
                return;
            // 连接入口的 parse 不能取消；其余 parse 可勾选，共享同一 transport。
            if (m_parserList->count() > 0)
            {
                for (int i = 0; i < m_parserList->count(); ++i)
                {
                    auto *item = m_parserList->item(i);
                    if (item->data(Qt::UserRole).toString() == m_preferredId &&
                        item->checkState() != Qt::Checked)
                    {
                        QSignalBlocker block(m_parserList);
                        item->setCheckState(Qt::Checked);
                        break;
                    }
                }
            }
            // 至少保留一个 parse；取消最后一个时恢复它。
            if (selectedParserCount() == 0)
            {
                QSignalBlocker block(m_parserList);
                for (int i = 0; i < m_parserList->count(); ++i)
                {
                    if (m_parserList->item(i)->data(Qt::UserRole).toString() == m_preferredId)
                    {
                        m_parserList->item(i)->setCheckState(Qt::Checked);
                        break;
                    }
                }
            }
            collectTransportValues();
            updateParserVisibility();
            updateTransportAvailability();
            buildTransportFields();
        });
    }
}

void DataInputDialog::rebuildParserSelection()
{
    if (!m_parserList || m_panes.size() <= 1)
        return;

    QStringList wanted;
    const QJsonArray initialParsers = m_initial.value(QStringLiteral("parsers")).toArray();
    for (const QJsonValue &v : initialParsers)
    {
        const QString id = v.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            wanted << id;
    }
    if (wanted.isEmpty())
        wanted << m_preferredId;

    QSignalBlocker block(m_parserList);
    for (int i = 0; i < m_parserList->count(); ++i)
    {
        auto *item = m_parserList->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        item->setCheckState(wanted.contains(id) ? Qt::Checked : Qt::Unchecked);
        const ParserPane *pane = nullptr;
        for (const ParserPane &p : m_panes)
        {
            if (p.id == id)
            {
                pane = &p;
                break;
            }
        }
        if (pane && !parserSupportsTransport(*pane, currentTransportType()))
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setCheckState(Qt::Unchecked);
        }
    }

    if (selectedParserCount() == 0)
    {
        for (int i = 0; i < m_parserList->count(); ++i)
        {
            if (m_parserList->item(i)->flags() & Qt::ItemIsEnabled)
            {
                m_parserList->item(i)->setCheckState(Qt::Checked);
                break;
            }
        }
    }
    updateTransportAvailability();
    updateParserVisibility();
}

void DataInputDialog::updateTransportAvailability()
{
    if (!m_parserList || m_panes.size() <= 1)
        return;
    const QString transport = currentTransportType();
    QSignalBlocker block(m_parserList);
    bool handled = false;
    for (int i = 0; i < m_parserList->count(); ++i)
    {
        auto *item = m_parserList->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        const ParserPane *pane = nullptr;
        for (const ParserPane &p : m_panes)
        {
            if (p.id == id)
            {
                pane = &p;
                break;
            }
        }
        if (!pane)
            continue;
        const bool ok = parserSupportsTransport(*pane, transport);
        item->setFlags(ok ? (item->flags() | Qt::ItemIsEnabled)
                          : (item->flags() & ~Qt::ItemIsEnabled));
        if (!ok)
            item->setCheckState(Qt::Unchecked);
        else if (item->checkState() == Qt::Checked)
            handled = true;
    }
    if (!handled)
    {
        for (int i = 0; i < m_parserList->count(); ++i)
        {
            auto *item = m_parserList->item(i);
            if (item->flags() & Qt::ItemIsEnabled)
            {
                item->setCheckState(Qt::Checked);
                break;
            }
        }
    }
    updateParserVisibility();
}

void DataInputDialog::updateParserVisibility()
{
    const bool multi = m_sourceMode && m_panes.size() > 1;
    int selected = 0;
    for (int i = 0; i < m_panes.size(); ++i)
    {
        bool show = true;
        if (multi && m_parserList && i < m_parserList->count())
            show = m_parserList->item(i)->checkState() == Qt::Checked;
        m_panes[i].box->setVisible(show);
        if (show)
            ++selected;
    }
    if (m_parserList)
        m_parserList->setVisible(multi);
    if (m_parserTitle)
        m_parserTitle->setText(multi ? QStringLiteral("解析协议（可多选，已选 %1）").arg(selected)
                                     : QStringLiteral("解析协议"));

    if (m_record)
    {
        bool recordable = false;
        for (int i = 0; i < m_panes.size(); ++i)
        {
            if (!multi || (m_parserList && i < m_parserList->count() &&
                           m_parserList->item(i)->checkState() == Qt::Checked))
                recordable = recordable || m_panes[i].recordable;
        }
        if (m_recordRow)
            m_recordRow->setVisible(recordable);
    }
}

QComboBox *DataInputDialog::comboFor(const ParserPane &pane, const QString &key) const
{
    return qobject_cast<QComboBox *>(pane.editors.value(key, nullptr));
}

QVariant DataInputDialog::currentValue(const ParserPane &pane, const QString &key) const
{
    QWidget *w = pane.editors.value(key, nullptr);
    if (auto *cb = qobject_cast<QCheckBox *>(w))
        return cb->isChecked();
    if (auto *sp = qobject_cast<QSpinBox *>(w))
        return sp->value();
    if (auto *dsp = qobject_cast<QDoubleSpinBox *>(w))
        return dsp->value();
    if (auto *co = qobject_cast<QComboBox *>(w))
        return co->currentData();
    if (auto *line = qobject_cast<QLineEdit *>(w))
        return line->text();
    return {};
}

void DataInputDialog::setCurrentValue(ParserPane &pane, const QString &key, const QVariant &value)
{
    QWidget *w = pane.editors.value(key, nullptr);
    if (!w)
        return;
    QSignalBlocker block(w);
    if (auto *cb = qobject_cast<QCheckBox *>(w))
        cb->setChecked(value.toBool());
    else if (auto *sp = qobject_cast<QSpinBox *>(w))
        sp->setValue(value.toInt());
    else if (auto *dsp = qobject_cast<QDoubleSpinBox *>(w))
        dsp->setValue(value.toDouble());
    else if (auto *co = qobject_cast<QComboBox *>(w))
    {
        int idx = co->findData(value);
        if (idx < 0 && value.canConvert<int>())
            idx = co->findData(value.toInt());
        if (idx < 0)
            idx = co->findData(value.toString());
        if (idx >= 0)
            co->setCurrentIndex(idx);
    }
    else if (auto *line = qobject_cast<QLineEdit *>(w))
        line->setText(value.toString());
}

QJsonObject DataInputDialog::parserOptions(const ParserPane &pane) const
{
    QJsonObject o;
    for (const Field &f : pane.fields)
    {
        if (!pane.editors.contains(f.key))
            continue;
        const QJsonObject vi = f.spec.value(QStringLiteral("visible_if")).toObject();
        if (!vi.isEmpty())
        {
            const QVariant cur = currentValue(pane, vi.value(QStringLiteral("key")).toString());
            if (cur.toString() != vi.value(QStringLiteral("equals")).toString())
                continue;
        }
        const QVariant v = currentValue(pane, f.key);
        if (f.type == QLatin1String("bool"))
            setNested(o, f.key, v.toBool());
        else if (f.type == QLatin1String("int"))
            setNested(o, f.key, v.toInt());
        else if (f.type == QLatin1String("double"))
            setNested(o, f.key, v.toDouble());
        else if (f.spec.value(QStringLiteral("source")).isObject())
            setNested(o, f.key, v.toInt());
        else
        {
            const QString s = v.toString();
            if (!s.isEmpty())
                setNested(o, f.key, s);
        }
    }
    return o;
}

const DataInputDialog::ParserPane *DataInputDialog::primaryPane() const
{
    const QStringList ids = selectedParserIds();
    const QString id = ids.contains(m_preferredId) ? m_preferredId
                                                   : (ids.isEmpty() ? QString() : ids.first());
    for (const ParserPane &pane : m_panes)
    {
        if (pane.id == id)
            return &pane;
    }
    return m_panes.isEmpty() ? nullptr : &m_panes.first();
}

DataInputDialog::ParserPane *DataInputDialog::primaryPane()
{
    const QStringList ids = selectedParserIds();
    const QString id = ids.contains(m_preferredId) ? m_preferredId
                                                   : (ids.isEmpty() ? QString() : ids.first());
    for (ParserPane &pane : m_panes)
    {
        if (pane.id == id)
            return &pane;
    }
    return m_panes.isEmpty() ? nullptr : &m_panes.first();
}

QStringList DataInputDialog::selectedParserIds() const
{
    QStringList ids;
    if (m_sourceMode && m_panes.size() > 1 && m_parserList)
    {
        for (int i = 0; i < m_parserList->count(); ++i)
        {
            if (m_parserList->item(i)->checkState() == Qt::Checked)
                ids << m_parserList->item(i)->data(Qt::UserRole).toString();
        }
        return ids;
    }
    for (const ParserPane &pane : m_panes)
        ids << pane.id;
    return ids;
}

int DataInputDialog::selectedParserCount() const
{
    return selectedParserIds().size();
}

QString DataInputDialog::primaryParserId() const
{
    const auto *pane = primaryPane();
    return pane ? pane->id : QString();
}

QJsonObject DataInputDialog::transportOptions() const
{
    QJsonObject o;
    for (auto it = m_transportEditors.constBegin(); it != m_transportEditors.constEnd(); ++it)
    {
        QWidget *w = it.value();
        if (auto *sp = qobject_cast<QSpinBox *>(w))
            o.insert(it.key(), sp->value());
        else if (auto *dsp = qobject_cast<QDoubleSpinBox *>(w))
            o.insert(it.key(), dsp->value());
        else if (auto *co = qobject_cast<QComboBox *>(w))
            o.insert(it.key(), co->currentData().toString());
        else if (auto *line = qobject_cast<QLineEdit *>(w))
            o.insert(it.key(), line->text());
        else if (auto *cb = qobject_cast<QCheckBox *>(w))
            o.insert(it.key(), cb->isChecked());
    }
    if (m_transport)
        o.insert(QStringLiteral("transport"), currentTransportType());
    return o;
}

QJsonObject DataInputDialog::currentPrimaryOptions() const
{
    const auto *pane = primaryPane();
    return pane ? parserOptions(*pane) : QJsonObject{};
}

QString DataInputDialog::optionsJson() const
{
    QJsonObject o = currentPrimaryOptions();
    if (m_sourceMode)
    {
        const QJsonObject transport = transportOptions();
        for (auto it = transport.constBegin(); it != transport.constEnd(); ++it)
            o.insert(it.key(), it.value());
        if (m_record)
        {
            o.insert(QStringLiteral("__record"), m_record->isChecked());
            if (m_recordFormat)
                o.insert(QStringLiteral("__record_format"), m_recordFormat->currentData().toString());
        }
    }
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString DataInputDialog::dataInputJson() const
{
    QJsonObject t;
    t.insert(QStringLiteral("type"), currentTransportType());
    const QJsonObject fields = transportOptions();
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
    {
        if (it.key() != QLatin1String("transport"))
            t.insert(it.key(), it.value());
    }
    if (currentTransportType() == QLatin1String("udp"))
    {
        t.insert(QStringLiteral("uri"),
                 QStringLiteral("udp://%1:%2")
                     .arg(t.value(QStringLiteral("bind")).toString())
                     .arg(t.value(QStringLiteral("port")).toInt()));
    }
    else if (currentTransportType() == QLatin1String("tcp"))
    {
        t.insert(QStringLiteral("uri"),
                 QStringLiteral("tcp://%1:%2")
                     .arg(t.value(QStringLiteral("host")).toString())
                     .arg(t.value(QStringLiteral("port")).toInt()));
    }
    else if (currentTransportType() == QLatin1String("file"))
    {
        t.insert(QStringLiteral("uri"),
                 QStringLiteral("file://%1").arg(t.value(QStringLiteral("filepath")).toString()));
    }

    QJsonArray parsers;
    for (const QString &id : selectedParserIds())
    {
        for (const ParserPane &pane : m_panes)
        {
            if (pane.id != id)
                continue;
            QJsonObject p;
            p.insert(QStringLiteral("id"), pane.id);
            p.insert(QStringLiteral("label"), pane.label);
            p.insert(QStringLiteral("options"), parserOptions(pane));
            parsers.append(p);
            break;
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("transport"), t);
    root.insert(QStringLiteral("parsers"), parsers);
    if (m_record)
    {
        root.insert(QStringLiteral("record"), m_record->isChecked());
        if (m_recordFormat)
            root.insert(QStringLiteral("record_format"), m_recordFormat->currentData().toString());
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DataInputDialog::applyInitialFromProbe(ParserPane &pane)
{
    for (const Field &f : pane.fields)
    {
        const QJsonObject init = f.spec.value(QStringLiteral("initial_from_probe")).toObject();
        if (init.isEmpty())
            continue;
        const QString key = init.value(QStringLiteral("probe_key")).toString();
        if (!pane.probeData.contains(key))
            continue;
        QVariant value = init.contains(QStringLiteral("set"))
                             ? init.value(QStringLiteral("set")).toVariant()
                             : probeVariant(pane.probeData, key);
        if (init.contains(QStringLiteral("trim")) && value.canConvert<QString>())
            value = value.toString().left(init.value(QStringLiteral("trim")).toInt());
        setCurrentValue(pane, f.key, value);
    }
}

void DataInputDialog::populateChoices(ParserPane &pane)
{
    for (const Field &f : pane.fields)
    {
        const QJsonObject src = f.spec.value(QStringLiteral("source")).toObject();
        if (src.isEmpty())
            continue;
        auto *cb = comboFor(pane, f.key);
        if (!cb)
            continue;
        const QString probeKey = src.value(QStringLiteral("probe_key")).toString();
        const QString labelKey = src.value(QStringLiteral("label_key")).toString();
        const QString filterKey = src.value(QStringLiteral("filter_key")).toString();
        const QString wanted = f.spec.value(QStringLiteral("filter_kind")).toString();
        const QJsonArray items = pane.probeData.value(probeKey).toArray();
        const int keep = cb->currentData().toInt();
        QSignalBlocker block(cb);
        cb->clear();
        for (int i = 0; i < items.size(); ++i)
        {
            const QJsonObject item = items.at(i).toObject();
            if (!wanted.isEmpty() && wanted != QLatin1String("all") && !filterKey.isEmpty() &&
                item.value(filterKey).toString() != wanted)
                continue;
            cb->addItem(item.value(labelKey).toString(), i);
        }
        if (keep >= 0)
        {
            const int idx = cb->findData(keep);
            if (idx >= 0)
                cb->setCurrentIndex(idx);
        }
        if (cb->count() > 0 && cb->currentIndex() < 0)
            cb->setCurrentIndex(0);
    }
}

void DataInputDialog::refreshProbe(bool initial)
{
    if (m_panes.isEmpty())
        return;
    ParserPane &pane = m_panes.first();
    if (!m_probeAvailable || !pane.probeFn)
    {
        if (m_summary)
            m_summary->setVisible(false);
        if (m_preview)
            m_preview->setVisible(false);
        if (m_warn)
            m_warn->setVisible(false);
        return;
    }

    QJsonObject opts = parserOptions(pane);
    QJsonObject probe = pane.probeFn(m_path, opts);
    if (probe.contains(QStringLiteral("error")))
    {
        m_warn->setVisible(true);
        m_warn->setText(probe.value(QStringLiteral("error")).toString());
        m_summary->setText(QStringLiteral("无法探测该文件"));
        m_preview->clear();
        return;
    }
    pane.probeData = probe;
    m_updating = true;
    if (initial)
        applyInitialFromProbe(pane);
    populateChoices(pane);
    if (initial)
        applyInitialFromProbe(pane);
    updateVisibility(pane);
    fillPreview();
    m_updating = false;
}

void DataInputDialog::updateVisibility(ParserPane &pane)
{
    for (const Field &f : pane.fields)
    {
        const QJsonObject vi = f.spec.value(QStringLiteral("visible_if")).toObject();
        if (vi.isEmpty())
            continue;
        const QVariant cur = currentValue(pane, vi.value(QStringLiteral("key")).toString());
        const bool show = cur.toString() == vi.value(QStringLiteral("equals")).toString();
        if (f.editor)
            f.editor->setVisible(show);
        if (f.label)
            f.label->setVisible(show);
    }
}

void DataInputDialog::fillPreview()
{
    if (m_panes.isEmpty())
        return;
    ParserPane &pane = m_panes.first();
    const QJsonObject ui = pane.schema.value(QStringLiteral("ui")).toObject();
    const QJsonObject preview = ui.value(QStringLiteral("preview")).toObject();
    if (preview.isEmpty())
    {
        m_summary->setVisible(false);
        m_preview->setVisible(false);
        m_warn->setVisible(false);
        return;
    }
    m_summary->setVisible(true);
    m_preview->setVisible(true);

    const QString columnsKey = preview.value(QStringLiteral("columns_key")).toString();
    const QString rowsKey = preview.value(QStringLiteral("rows_key")).toString();
    const QString labelKey = preview.value(QStringLiteral("label_key")).toString();
    const QJsonArray cols = pane.probeData.value(columnsKey).toArray();
    const QJsonArray rows = pane.probeData.value(rowsKey).toArray();

    QString summary = preview.value(QStringLiteral("summary")).toString();
    if (summary.isEmpty())
        summary = QStringLiteral("{column_count} 列 · {row_count} 行");
    summary.replace(QStringLiteral("{column_count}"), QString::number(cols.size()));
    summary.replace(QStringLiteral("{row_count}"), QString::number(rows.size()));
    m_summary->setText(summary);

    m_preview->clear();
    m_preview->setColumnCount(cols.size() + 1);
    QStringList heads{QStringLiteral("#")};
    for (const QJsonValue &cv : cols)
        heads << cv.toObject().value(labelKey).toString();
    m_preview->setHorizontalHeaderLabels(heads);
    m_preview->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
    {
        auto *idx = new QTableWidgetItem(QString::number(r));
        idx->setForeground(QColor(QStringLiteral("#6e7681")));
        m_preview->setItem(r, 0, idx);
        const QJsonArray cells = rows.at(r).toArray();
        for (int c = 0; c < cols.size(); ++c)
        {
            const QString v = c < cells.size() ? cells.at(c).toString() : QString();
            m_preview->setItem(r, c + 1, new QTableWidgetItem(v));
        }
    }
    m_preview->resizeColumnsToContents();
    m_preview->setColumnWidth(0, 40);
    const int rowCount = qMax(1, m_preview->rowCount());
    m_preview->setFixedHeight(m_preview->horizontalHeader()->height() + rowCount * 20 + 4);

    QStringList warns;
    const QString warningsKey = preview.value(QStringLiteral("warnings_key")).toString();
    for (const QJsonValue &w : pane.probeData.value(warningsKey).toArray())
        warns << w.toString();
    for (const QJsonValue &rv : ui.value(QStringLiteral("warnings")).toArray())
    {
        const QJsonObject r = rv.toObject();
        const QString probeKey = r.value(QStringLiteral("probe_key")).toString();
        const QString fieldKey = r.value(QStringLiteral("field_key")).toString();
        if (!pane.probeData.contains(probeKey) || !pane.editors.contains(fieldKey))
            continue;
        const QString probeText = probeVariant(pane.probeData, probeKey).toString();
        const QString expectProbe = r.value(QStringLiteral("probe_equals")).toVariant().toString();
        const QString fieldText = currentValue(pane, fieldKey).toString();
        const QString expectField = r.value(QStringLiteral("field_equals")).toVariant().toString();
        if (probeText == expectProbe && fieldText == expectField)
            warns << r.value(QStringLiteral("text")).toString();
    }
    m_warn->setVisible(!warns.isEmpty());
    m_warn->setText(warns.join(QStringLiteral("  ·  ")));
}
