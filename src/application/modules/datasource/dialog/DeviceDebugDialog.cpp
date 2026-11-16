#include "DeviceDebugDialog.h"

#include "DataSourceEntry.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {

QJsonArray pickArray(const QJsonObject &debug, const QJsonObject &schema, const QString &key)
{
    const QJsonArray fromDebug = debug.value(key).toArray();
    return fromDebug.isEmpty() ? schema.value(key).toArray() : fromDebug;
}

QString pickString(const QJsonObject &debug, const QJsonObject &schema, const QString &key,
                   const QString &fallback = QString())
{
    const QString fromDebug = debug.value(key).toString();
    if (!fromDebug.isEmpty())
        return fromDebug;
    const QString fromSchema = schema.value(key).toString();
    return fromSchema.isEmpty() ? fallback : fromSchema;
}

} // namespace

DeviceDebugDialog::DeviceDebugDialog(const DataSourceEntry &source, const QString &instanceName,
                                     std::function<QString()> statusFn,
                                     std::function<QString(const QString &)> sendFn,
                                     QWidget *parent)
    : QDialog(parent), m_statusFn(std::move(statusFn)), m_sendFn(std::move(sendFn))
{
    const QJsonObject debug = source.schema.value(QStringLiteral("debug")).toObject();
    const QString title = pickString(debug, source.schema, QStringLiteral("title"),
                                     QStringLiteral("设备调试"));
    const QString hint = pickString(debug, source.schema, QStringLiteral("hint"),
                                    QStringLiteral("发送命令后，设备响应显示在下方。"));
    const QString name = instanceName.isEmpty() ? source.label : instanceName;

    const QJsonArray commands = pickArray(debug, source.schema, QStringLiteral("commands"));
    m_statusFields = pickArray(debug, source.schema, QStringLiteral("status"));

    setWindowTitle(QStringLiteral("%1 · %2").arg(title, name));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(760, 520);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    auto *head = new QLabel(this);
    head->setText(QStringLiteral("%1\n%2").arg(name, hint));
    head->setStyleSheet(QStringLiteral("color:#8b98a5;font-size:11px;"));
    lay->addWidget(head);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    m_presets = new QComboBox(this);
    m_presets->setMinimumWidth(190);
    for (const QJsonValue &v : commands)
    {
        const QJsonObject c = v.toObject();
        m_presets->addItem(c.value(QStringLiteral("label")).toString(),
                           c.value(QStringLiteral("command")).toString());
    }
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入调试命令"));
    m_send = new QPushButton(QStringLiteral("发送"), this);
    m_send->setDefault(true);
    row->addWidget(m_presets);
    row->addWidget(m_input, 1);
    row->addWidget(m_send);
    lay->addLayout(row);

    m_state = new QLabel(this);
    m_state->setStyleSheet(QStringLiteral("color:#5d6c77;font-size:11px;"));
    lay->addWidget(m_state);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(600);
    m_log->setStyleSheet(QStringLiteral("QPlainTextEdit{background:#0d1117;color:#c9d1d9;"
                                        "font-family:'JetBrains Mono','Consolas',monospace;"
                                        "font-size:11px;border:1px solid #202a33;}"));
    lay->addWidget(m_log, 1);

    m_hasCommand = static_cast<bool>(m_sendFn);
    m_send->setEnabled(m_hasCommand);
    m_input->setEnabled(m_hasCommand);
    m_presets->setEnabled(m_hasCommand && !commands.isEmpty());
    connect(m_presets, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0)
            m_input->setText(m_presets->itemData(i).toString());
    });
    connect(m_send, &QPushButton::clicked, this, &DeviceDebugDialog::sendCurrent);
    connect(m_input, &QLineEdit::returnPressed, this, &DeviceDebugDialog::sendCurrent);

    if (m_statusFn)
    {
        auto *timer = new QTimer(this);
        timer->setInterval(500);
        connect(timer, &QTimer::timeout, this, &DeviceDebugDialog::refreshStatus);
        timer->start();
    }
    refreshStatus();
}

void DeviceDebugDialog::append(const QString &text)
{
    if (text.isEmpty())
        return;
    m_log->appendPlainText(QStringLiteral("[%1] %2")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

QString DeviceDebugDialog::statusValueText(const QJsonValue &value, const QJsonObject &field) const
{
    const QString type = field.value(QStringLiteral("type")).toString();
    if (value.isBool())
    {
        if (type == QLatin1String("bool"))
        {
            const QString yes = field.value(QStringLiteral("true")).toString(QStringLiteral("是"));
            const QString no = field.value(QStringLiteral("false")).toString(QStringLiteral("否"));
            return value.toBool() ? yes : no;
        }
        return value.toBool() ? QStringLiteral("是") : QStringLiteral("否");
    }
    if (value.isDouble())
    {
        QString text = QString::number(value.toDouble());
        const QString suffix = field.value(QStringLiteral("suffix")).toString();
        return suffix.isEmpty() ? text : text + suffix;
    }
    if (value.isString())
        return value.toString();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QStringLiteral("-");
}

void DeviceDebugDialog::sendCurrent()
{
    if (!m_sendFn)
        return;
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty())
        return;

    const QString res = m_sendFn(cmd);
    const QJsonObject obj = QJsonDocument::fromJson(res.toUtf8()).object();
    if (obj.contains(QStringLiteral("ok")))
    {
        if (obj.value(QStringLiteral("ok")).toBool())
            append(QStringLiteral("TX  %1").arg(obj.value(QStringLiteral("command")).toString(cmd)));
        else
            append(QStringLiteral("ERR %1").arg(obj.value(QStringLiteral("error")).toString(res)));
    }
    else
    {
        append(QStringLiteral("TX  %1").arg(cmd));
        const QString rx = res.trimmed();
        if (!rx.isEmpty())
            append(QStringLiteral("RX  %1").arg(rx));
    }
    m_lastRx.clear();
    refreshStatus();
}

void DeviceDebugDialog::refreshStatus()
{
    if (!m_statusFn)
    {
        m_state->setText(QStringLiteral("未提供状态通道"));
        return;
    }
    const QJsonObject s = QJsonDocument::fromJson(m_statusFn().toUtf8()).object();
    if (s.isEmpty())
    {
        m_state->setText(QStringLiteral("状态不可用"));
        return;
    }

    QStringList parts;
    if (!m_statusFields.isEmpty())
    {
        for (const QJsonValue &v : m_statusFields)
        {
            const QJsonObject field = v.toObject();
            const QString key = field.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                continue;
            const QString label = field.value(QStringLiteral("label")).toString(key);
            parts << QStringLiteral("%1 %2").arg(label, statusValueText(s.value(key), field));
        }
    }
    else
    {
        // 没有 schema.status 时，自动平铺状态 JSON 的标量字段；last_rx 只走 RX 日志。
        for (auto it = s.constBegin(); it != s.constEnd(); ++it)
        {
            if (it.key() == QLatin1String("last_rx"))
                continue;
            if (it.value().isObject() || it.value().isArray())
                continue;
            parts << QStringLiteral("%1 %2").arg(it.key(), statusValueText(it.value(), QJsonObject{}));
        }
    }
    m_state->setText(parts.isEmpty() ? QStringLiteral("状态不可用") : parts.join(QStringLiteral(" · ")));

    const QString rx = s.value(QStringLiteral("last_rx")).toString();
    if (!rx.isEmpty() && rx != m_lastRx)
    {
        m_lastRx = rx;
        append(QStringLiteral("RX  %1").arg(rx));
    }
}
