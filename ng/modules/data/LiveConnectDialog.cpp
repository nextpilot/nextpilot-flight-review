#include "LiveConnectDialog.h"

#include "Intent.h"
#include "ParserRegistry.h"
#include "Result.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::intent;
using nextpilot::plotscope::parser::ParserRegistry;

namespace nextpilot::plotscope::gui
{

LiveConnectDialog::LiveConnectDialog(IIntentExecutor &executor, QWidget *parent)
    : QDialog(parent), m_executor(executor)
{
    setWindowTitle(QStringLiteral("连接实时源"));
    setModal(true);
    buildUi();
    updateFieldVisibility();
}

void LiveConnectDialog::buildUi()
{
    setMinimumWidth(430);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 12);
    lay->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("接一个正在输出数据的源"), this);
    title->setObjectName(QStringLiteral("sectionTitle"));
    lay->addWidget(title);
    auto *sub = new QLabel(
        QStringLiteral("字节到达即按解析器落库；断流不会关掉数据源，可随时暂停/重连。"), this);
    sub->setObjectName(QStringLiteral("textDim"));
    sub->setWordWrap(true);
    lay->addWidget(sub);

    // 表单用手排 vbox 而不是 QFormLayout：Qt 6.2 没有 setRowVisible，用 form label
    // 的话隐藏控件行会留下空标签与空行高，UDP/串口字段互斥时露出空白。
    auto *formHost = new QWidget(this);
    auto *formV = new QVBoxLayout(formHost);
    formV->setContentsMargins(0, 0, 0, 0);
    formV->setSpacing(8);
    auto addRow = [this, formHost, formV](const QString &label, QWidget *field,
                                         QWidget *trailing = nullptr) {
        QWidget *row = new QWidget(formHost);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        auto *lab = new QLabel(label, row);
        lab->setFixedWidth(48);
        h->addWidget(lab);
        h->addWidget(field, 1);
        if (trailing)
            h->addWidget(trailing);
        formV->addWidget(row);
        return row;
    };

    m_transport = new QComboBox(formHost);
    m_transport->addItems({QStringLiteral("UDP"), QStringLiteral("TCP"), QStringLiteral("串口")});
    addRow(QStringLiteral("传输"), m_transport);

    // host：0.0.0.0 / 留空 = 监听（TCP 等待对方连入）；其余 = 主动连过去。
    m_host = new QLineEdit(formHost);
    m_host->setText(QStringLiteral("0.0.0.0"));
    m_host->setPlaceholderText(QStringLiteral("0.0.0.0 = 监听"));
    m_hostRow = addRow(QStringLiteral("地址"), m_host);

    m_port = new QSpinBox(formHost);
    m_port->setRange(1, 65535);
    m_port->setValue(14550);
    m_portRow = addRow(QStringLiteral("端口"), m_port);

    m_device = new QLineEdit(formHost);
    m_device->setPlaceholderText(QStringLiteral("/dev/ttyUSB0 或 COM3"));
    m_browseDev = new QPushButton(QStringLiteral("浏览"), formHost);
    m_browseDev->setFixedWidth(64);
    m_deviceRow = addRow(QStringLiteral("设备"), m_device, m_browseDev);

    m_baud = new QComboBox(formHost);
    m_baud->addItems({QStringLiteral("115200"), QStringLiteral("57600"), QStringLiteral("921600"),
                      QStringLiteral("460800"), QStringLiteral("230400"), QStringLiteral("38400"),
                      QStringLiteral("9600")});
    m_baud->setEditable(true);
    m_baudRow = addRow(QStringLiteral("波特率"), m_baud);

    m_parser = new QComboBox(formHost);
    // 只列已注册的解析器，不硬编码——扩展包注册后自动出现。
    for (const QString &id : ParserRegistry::ids())
        m_parser->addItem(id);
    if (m_parser->count() == 0)
        m_parser->addItem(QStringLiteral("custom-protocol"));
    else if (const int cp = m_parser->findText(QStringLiteral("custom-protocol")); cp >= 0)
        m_parser->setCurrentIndex(cp); // 实时流默认走模板化字节解析，不是行式 csv
    addRow(QStringLiteral("解析器"), m_parser);

    m_template = new QLineEdit(formHost);
    m_template->setPlaceholderText(QStringLiteral("协议模板（custom-protocol 必填）"));
    m_browseTpl = new QPushButton(QStringLiteral("选择"), formHost);
    m_browseTpl->setFixedWidth(64);
    m_templateRow = addRow(QStringLiteral("模板"), m_template, m_browseTpl);

    lay->addWidget(formHost);

    m_urlPreview = new QLabel(this);
    m_urlPreview->setObjectName(QStringLiteral("textDim"));
    m_urlPreview->setWordWrap(true);
    m_urlPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_urlPreview);

    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color:#e05c5c;"));
    m_error->hide();
    lay->addWidget(m_error);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setFixedWidth(80);
    m_ok = new QPushButton(QStringLiteral("连接"), this);
    m_ok->setFixedWidth(96);
    m_ok->setDefault(true);
    btnRow->addWidget(cancel);
    btnRow->addWidget(m_ok);
    lay->addLayout(btnRow);

    connect(m_transport, &QComboBox::currentIndexChanged, this, [this] { updateFieldVisibility(); });
    connect(m_port, &QSpinBox::valueChanged, this, [this] { updateUrlPreview(); });
    connect(m_host, &QLineEdit::textChanged, this, [this] { updateUrlPreview(); });
    connect(m_device, &QLineEdit::textChanged, this, [this] { updateUrlPreview(); });
    connect(m_baud, &QComboBox::currentTextChanged, this, [this] { updateUrlPreview(); });
    connect(m_parser, &QComboBox::currentTextChanged, this, [this] { updateFieldVisibility(); });
    connect(m_browseDev, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择串口设备"), QString(),
                                                       QStringLiteral("设备 (*)"));
        if (!f.isEmpty())
            m_device->setText(f);
    });
    connect(m_browseTpl, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择协议模板"), QString(),
            QStringLiteral("协议模板 (*.json);;所有文件 (*)"));
        if (!f.isEmpty())
            m_template->setText(f);
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QPushButton::clicked, this, &LiveConnectDialog::submit);
}

void LiveConnectDialog::updateFieldVisibility()
{
    const int t = m_transport->currentIndex();
    const bool serial = t == Serial;
    m_hostRow->setVisible(!serial);
    m_portRow->setVisible(!serial);
    m_deviceRow->setVisible(serial);
    m_baudRow->setVisible(serial);
    // 模板只在解析器明确要求时露出来（当前只有 custom-protocol 走模板）。
    m_templateRow->setVisible(m_parser->currentText() == QLatin1String("custom-protocol"));
    // 切传输/切解析器后上一次的报错不再成立，直接收掉，避免红字常驻。
    if (m_error)
        m_error->hide();
    updateUrlPreview();
}

QString LiveConnectDialog::composeUrl() const
{
    switch (m_transport->currentIndex())
    {
    case Udp:
        return QStringLiteral("udp://%1:%2").arg(m_host->text().trimmed()).arg(m_port->value());
    case Tcp:
        return QStringLiteral("tcp://%1:%2").arg(m_host->text().trimmed()).arg(m_port->value());
    case Serial:
    {
        const QString dev = m_device->text().trimmed();
        if (dev.isEmpty())
            return QString();
        return QStringLiteral("serial://%1:%2").arg(dev).arg(m_baud->currentText().trimmed());
    }
    default:
        return QString();
    }
}

void LiveConnectDialog::updateUrlPreview()
{
    const QString url = composeUrl();
    if (url.isEmpty())
    {
        m_urlPreview->setText(m_transport->currentIndex() == Serial
                                  ? QStringLiteral("填写串口设备与波特率后这里会显示最终 url")
                                  : QStringLiteral("填写地址与端口后这里会显示最终 url"));
        return;
    }
    QString hint;
    if (m_transport->currentIndex() == Tcp
        && (m_host->text().trimmed().isEmpty()
            || m_host->text().trimmed() == QLatin1String("0.0.0.0")))
        hint = QStringLiteral("　（监听模式：等待对方连入）");
    else if (m_transport->currentIndex() == Udp)
        hint = QStringLiteral("　（监听端口：任何发送端都可打到这里）");
    m_urlPreview->setText(url + hint);
}

void LiveConnectDialog::submit()
{
    m_error->hide();
    const QString url = composeUrl();
    if (url.isEmpty())
    {
        m_error->setText(QStringLiteral("请填写端点（串口需要设备路径）"));
        m_error->show();
        return;
    }
    const QString parser = m_parser->currentText();
    if (parser.isEmpty())
    {
        m_error->setText(QStringLiteral("请选择解析器"));
        m_error->show();
        return;
    }
    if (parser == QLatin1String("custom-protocol") && m_template->text().trimmed().isEmpty())
    {
        m_error->setText(QStringLiteral("custom-protocol 需要一个协议模板，请选择模板文件"));
        m_error->show();
        return;
    }

    Intent intent;
    intent.op = QStringLiteral("data.connect");
    intent.params[QStringLiteral("url")] = url;
    intent.params[QStringLiteral("parser")] = parser;
    if (!m_template->text().trimmed().isEmpty())
        intent.params[QStringLiteral("templatePath")] = m_template->text().trimmed();

    const auto r = m_executor.execute(intent);
    if (!r)
    {
        // 错误就地显示：不再弹第二个框，用户改字段即可重试。
        m_error->setText(r.error().message);
        m_error->show();
        return;
    }
    m_connectedUrl = url;
    accept();
}

} // namespace nextpilot::plotscope::gui
