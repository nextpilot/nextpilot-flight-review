#include "data_input/SourceConfigDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SourceConfigDialog::SourceConfigDialog(const SourcePlugin &plugin, const QString &initialConfig,
                                       QWidget *parent)
    : QDialog(parent), m_plugin(plugin)
{
    setWindowTitle(QStringLiteral("连接 · %1").arg(plugin.label));
    resize(420, 180);
    auto *root = new QVBoxLayout(this);
    auto *head = new QLabel(QStringLiteral("%1  ·  %2 / %3")
                                .arg(plugin.label, plugin.kind, plugin.transport), this);
    head->setStyleSheet(QStringLiteral("color:#8494a0;font-size:11px;"));
    root->addWidget(head);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    QJsonObject defaults = QJsonDocument::fromJson(plugin.defaultConfig().toUtf8()).object();
    const QJsonObject initial = QJsonDocument::fromJson(initialConfig.toUtf8()).object();
    for (auto it = initial.constBegin(); it != initial.constEnd(); ++it)
        defaults.insert(it.key(), it.value());
    for (const QJsonValue &fv : plugin.schema.value(QStringLiteral("fields")).toArray())
    {
        const QJsonObject f = fv.toObject();
        const QString key = f.value(QStringLiteral("key")).toString();
        if (key.isEmpty())
            continue;
        const QString type = f.value(QStringLiteral("type")).toString(QStringLiteral("string"));
        const QString label = f.value(QStringLiteral("label")).toString(key);
        const QJsonValue def = defaults.contains(key) ? defaults.value(key)
                                                       : f.value(QStringLiteral("default"));
        QWidget *editor = nullptr;
        if (type == QLatin1String("bool"))
        {
            auto *w = new QCheckBox(f.value(QStringLiteral("hint")).toString(), this);
            w->setChecked(def.toBool());
            editor = w;
        }
        else if (type == QLatin1String("enum"))
        {
            auto *w = new QComboBox(this);
            for (const QJsonValue &ov : f.value(QStringLiteral("options")).toArray())
            {
                const QJsonObject o = ov.toObject();
                const QString value = o.contains(QStringLiteral("value"))
                                          ? o.value(QStringLiteral("value")).toString()
                                          : ov.toString();
                w->addItem(o.value(QStringLiteral("label")).toString(value), value);
            }
            const int idx = w->findData(def.toString());
            if (idx >= 0)
                w->setCurrentIndex(idx);
            editor = w;
        }
        else if (type == QLatin1String("int"))
        {
            auto *w = new QSpinBox(this);
            w->setRange(f.value(QStringLiteral("min")).toInt(-1000000),
                        f.value(QStringLiteral("max")).toInt(1000000));
            w->setValue(def.toInt(f.value(QStringLiteral("default")).toInt()));
            if (f.contains(QStringLiteral("suffix")))
                w->setSuffix(f.value(QStringLiteral("suffix")).toString());
            editor = w;
        }
        else if (type == QLatin1String("double"))
        {
            auto *w = new QDoubleSpinBox(this);
            w->setRange(f.value(QStringLiteral("min")).toDouble(-1e9),
                        f.value(QStringLiteral("max")).toDouble(1e9));
            w->setDecimals(f.value(QStringLiteral("decimals")).toInt(2));
            w->setValue(def.toDouble(f.value(QStringLiteral("default")).toDouble()));
            editor = w;
        }
        else
        {
            auto *w = new QLineEdit(def.toString(), this);
            w->setPlaceholderText(f.value(QStringLiteral("placeholder")).toString());
            editor = w;
        }
        if (editor)
        {
            m_editors.insert(key, editor);
            form->addRow(label, editor);
        }
    }
    if (plugin.replayable)
    {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        m_replayPath = new QLineEdit(initial.value(QStringLiteral("filepath")).toString(), row);
        m_replayPath->setPlaceholderText(QStringLiteral("留空=实时连接；选择文件=同插件回放原始字节"));
        auto *browse = new QPushButton(QStringLiteral("浏览…"), row);
        h->addWidget(m_replayPath, 1);
        h->addWidget(browse);
        form->addRow(QStringLiteral("回放原始文件"), row);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString filter = m_plugin.replayExtensions.isEmpty()
                                       ? QStringLiteral("原始流 (*.bin *.dat *.mav *.log);;全部 (*)")
                                       : QStringLiteral("原始流 (%1);;全部 (*)").arg(m_plugin.replayExtensions);
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择原始流文件"),
                                                             QString(), filter);
            if (!path.isEmpty())
                m_replayPath->setText(path);
        });
    }
    root->addLayout(form);
    if (plugin.recordable)
    {
        m_record = new QCheckBox(QStringLiteral("连接后记录到 CSV（host 侧记录解码通道）"), this);
        root->addWidget(m_record);
    }
    root->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QString SourceConfigDialog::configJson() const
{
    QJsonObject o;
    for (auto it = m_editors.constBegin(); it != m_editors.constEnd(); ++it)
    {
        QWidget *w = it.value();
        if (auto *cb = qobject_cast<QCheckBox *>(w))
            o.insert(it.key(), cb->isChecked());
        else if (auto *combo = qobject_cast<QComboBox *>(w))
            o.insert(it.key(), combo->currentData().toString());
        else if (auto *spin = qobject_cast<QSpinBox *>(w))
            o.insert(it.key(), spin->value());
        else if (auto *dspin = qobject_cast<QDoubleSpinBox *>(w))
            o.insert(it.key(), dspin->value());
        else if (auto *line = qobject_cast<QLineEdit *>(w))
            o.insert(it.key(), line->text());
    }
    if (m_record)
        o.insert(QStringLiteral("__record"), m_record->isChecked());
    if (m_replayPath && !m_replayPath->text().trimmed().isEmpty())
        o.insert(QStringLiteral("filepath"), m_replayPath->text().trimmed());
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
