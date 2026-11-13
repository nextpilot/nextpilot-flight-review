#include "data_input/SourceDebugDialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

SourceDebugDialog::SourceDebugDialog(const QString &title, const QJsonObject &schema,
                                     std::function<QString()> statusFn,
                                     std::function<QString(const QString &)> sendFn,
                                     QWidget *parent)
    : QDialog(parent), m_statusFn(std::move(statusFn)), m_sendFn(std::move(sendFn))
{
    setWindowTitle(QStringLiteral("调试 · %1").arg(title));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(760, 520);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    auto *head = new QLabel(this);
    head->setText(QStringLiteral("%1\n%2")
                      .arg(title, schema.value(QStringLiteral("hint"))
                                      .toString(QStringLiteral("发送命令后，接收机响应显示在下方。"))));
    head->setStyleSheet(QStringLiteral("color:#8b98a5;font-size:11px;"));
    lay->addWidget(head);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    m_presets = new QComboBox(this);
    m_presets->setMinimumWidth(190);
    const QJsonArray commands = schema.value(QStringLiteral("commands")).toArray();
    for (const QJsonValue &v : commands)
    {
        const QJsonObject c = v.toObject();
        m_presets->addItem(c.value(QStringLiteral("label")).toString(),
                           c.value(QStringLiteral("command")).toString());
    }
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入命令，例如 VERSION / LOG BESTPOSA ONCHANGED"));
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
    connect(m_send, &QPushButton::clicked, this, &SourceDebugDialog::sendCurrent);
    connect(m_input, &QLineEdit::returnPressed, this, &SourceDebugDialog::sendCurrent);

    auto *timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &SourceDebugDialog::refreshStatus);
    timer->start();
    refreshStatus();
}

void SourceDebugDialog::append(const QString &text)
{
    if (text.isEmpty())
        return;
    m_log->appendPlainText(QStringLiteral("[%1] %2")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void SourceDebugDialog::sendCurrent()
{
    if (!m_sendFn)
        return;
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty())
        return;
    const QString res = m_sendFn(cmd);
    const QJsonObject obj = QJsonDocument::fromJson(res.toUtf8()).object();
    if (obj.value(QStringLiteral("ok")).toBool())
        append(QStringLiteral("TX  %1").arg(obj.value(QStringLiteral("command")).toString(cmd)));
    else
        append(QStringLiteral("ERR %1").arg(obj.value(QStringLiteral("error")).toString(res)));
    m_lastRx.clear();
    refreshStatus();
}

void SourceDebugDialog::refreshStatus()
{
    if (!m_statusFn)
        return;
    const QJsonObject s = QJsonDocument::fromJson(m_statusFn().toUtf8()).object();
    if (s.isEmpty())
    {
        m_state->setText(QStringLiteral("状态不可用"));
        return;
    }
    m_state->setText(QStringLiteral("%1 · tx %2 · lines %3 · logs %4 · frames %5 · 协议库 %6")
                         .arg(s.value(QStringLiteral("state")).toString(QStringLiteral("-")))
                         .arg(s.value(QStringLiteral("tx")).toInt())
                         .arg(s.value(QStringLiteral("lines")).toInt())
                         .arg(s.value(QStringLiteral("logs")).toInt())
                         .arg(s.value(QStringLiteral("frames")).toInt())
                         .arg(s.value(QStringLiteral("db")).toBool() ? QStringLiteral("loaded")
                                                                      : QStringLiteral("-")));
    const QString rx = s.value(QStringLiteral("last_rx")).toString();
    if (!rx.isEmpty() && rx != m_lastRx)
    {
        m_lastRx = rx;
        append(QStringLiteral("RX  %1").arg(rx));
    }
}
