#include "ProtocolInspectorDialog.h"

#include "ThemeManager.h"

#include "DecodedFrame.h"
#include "ToolboxHub.h"
#include "Domain.h"
#include "IMessageParser.h"
#include "QtFieldRenderer.h"
#include "SchemaEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

namespace nextpilot::plotscope::gui
{

ProtocolInspectorDialog::ProtocolInspectorDialog(nextpilot::plotscope::registry::ToolboxHub &registry,
                                                 QWidget *parent)
    : QDialog(parent), m_registry(registry)
{
    setWindowTitle(QStringLiteral("协议检查器"));
    resize(980, 640);

    // --- 左侧：协议列表（自述）---
    m_protocolList = new QListWidget;
    m_protocolList->setMinimumWidth(220);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(QStringLiteral("协议（自述）")));
    left->addWidget(m_protocolList, 1);
    left->addWidget(new QLabel(QStringLiteral("使用范围")));
    left->addWidget(m_info);

    // --- 右上：参数 + 来源 ---
    m_transport = new QComboBox;
    m_paramHost = new QWidget;
    m_paramForm = new QFormLayout(m_paramHost);

    m_path = new QLineEdit;
    m_path->setPlaceholderText(QStringLiteral("选择要检查的字节文件…"));
    auto *browse = new QPushButton(QStringLiteral("浏览…"));
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);

    m_input = new QPlainTextEdit;
    m_input->setPlaceholderText(QStringLiteral("字节文本（可粘贴，如 $GPGGA,...*47）——留空则读上面的文件"));
    m_input->setMinimumHeight(90);

    auto *parseBtn = new QPushButton(QStringLiteral("解析"));
    m_status = new QLabel;

    auto *rightTop = new QVBoxLayout;
    rightTop->addWidget(new QLabel(QStringLiteral("传输协议")));
    rightTop->addWidget(m_transport);
    rightTop->addWidget(new QLabel(QStringLiteral("配置参数（configSchema）")));
    rightTop->addWidget(m_paramHost);
    rightTop->addWidget(new QLabel(QStringLiteral("数据来源")));
    rightTop->addLayout(pathRow);
    rightTop->addWidget(m_input);
    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(parseBtn);
    btnRow->addWidget(m_status, 1);
    rightTop->addLayout(btnRow);

    auto *rightGroup = new QGroupBox(QStringLiteral("解析设置"));
    rightGroup->setLayout(rightTop);

    // --- 右下：字段树 ---
    m_fields = new QTreeWidget;
    m_fields->setColumnCount(4);
    m_fields->setHeaderLabels({QStringLiteral("字段"), QStringLiteral("原值"),
                               QStringLiteral("可读值"), QStringLiteral("单位/说明")});

    auto *right = new QVBoxLayout;
    right->addWidget(rightGroup);
    right->addWidget(new QLabel(QStringLiteral("解码结果（DecodedFrame）")));
    right->addWidget(m_fields, 1);

    auto *mainLay = new QHBoxLayout(this);
    mainLay->addLayout(left);
    mainLay->addLayout(right, 2);

    // --- 信号 ---
    connect(m_protocolList, &QListWidget::currentRowChanged, this,
            [this](int) { onProtocolChanged(); });
    connect(browse, &QPushButton::clicked, this, &ProtocolInspectorDialog::chooseFile);
    connect(parseBtn, &QPushButton::clicked, this, &ProtocolInspectorDialog::doParse);

    applyTheme();
    rebuildProtocolList();
}

ProtocolInspectorDialog::~ProtocolInspectorDialog() = default;

void ProtocolInspectorDialog::applyTheme()
{
    const QString bg = ThemeManager::themeColor(QStringLiteral("panel")).name();
    const QString fg = ThemeManager::themeColor(QStringLiteral("text")).name();
    const QString dim = ThemeManager::themeColor(QStringLiteral("textDim")).name();
    setStyleSheet(QStringLiteral(
        "QDialog{background:%1;color:%2;}"
        "QLabel{color:%3;}"
        "QGroupBox{color:%2;border:1px solid %3;border-radius:4px;margin-top:8px;}"
        "QGroupBox::title{color:%2;}")
        .arg(bg, fg, dim));
    m_fields->setStyleSheet(QStringLiteral(
        "QTreeWidget{background:%1;color:%2;alternate-background-color:%1;}"
        "QHeaderView::section{background:%1;color:%2;}")
        .arg(bg, fg));
}

void ProtocolInspectorDialog::rebuildProtocolList()
{
    m_protocolList->clear();
    const auto parsers = m_registry.messageParsers();
    for (nextpilot::plotscope::parser::IMessageParser *p : parsers)
    {
        auto *item = new QListWidgetItem(QStringLiteral("%1  ·  %2")
                                             .arg(p->protocolName(), nextpilot::plotscope::registry::domainName(p->domain())));
        item->setData(Qt::UserRole, p->id());
        m_protocolList->addItem(item);
    }
    if (m_protocolList->count() > 0)
        m_protocolList->setCurrentRow(0);
    else
        m_info->setText(QStringLiteral("工具箱中没有已登记的协议解析器。"));
}

void ProtocolInspectorDialog::onProtocolChanged()
{
    auto *item = m_protocolList->currentItem();
    m_current = item ? m_registry.messageParser(item->data(Qt::UserRole).toString()) : nullptr;
    if (!m_current)
    {
        m_info->clear();
        return;
    }

    // 自述信息 → 说明区（使用范围：领域 + 用途）。
    m_info->setText(QStringLiteral("%1\n\n%2\n\n支持传输：%3")
                        .arg(m_current->protocolName(),
                             m_current->usage(),
                             m_current->supportedTransports().join(QStringLiteral(", "))));

    // 自述 → 传输下拉。
    m_transport->clear();
    m_transport->addItems(m_current->supportedTransports());

    rebuildParamFields();
    m_fields->clear();
}

void ProtocolInspectorDialog::rebuildParamFields()
{
    while (m_paramForm->rowCount() > 0)
        m_paramForm->removeRow(0);
    if (!m_current)
        return;

    // 参数区走统一 uischema：解析器只写 paramSpecs()，控件映射全在渲染器。
    using namespace nextpilot::plotscope::uischema;
    QHash<QString, QString> labels;
    const QJsonArray fields = m_current->configSchema().value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &v : fields)
    {
        const QJsonObject f = v.toObject();
        const QString key = f.value(QStringLiteral("key")).toString();
        const QString label = f.value(QStringLiteral("label")).toString();
        if (!key.isEmpty() && !label.isEmpty())
            labels.insert(key, label);
    }
    m_paramFormModel = SchemaEngine::compile(m_current->protocolName(),
                                             m_current->paramSpecs(), labels);
    QtFieldRenderer::render(m_paramFormModel, m_paramForm, m_paramHost);
}

QJsonObject ProtocolInspectorDialog::collectParams() const
{
    using namespace nextpilot::plotscope::uischema;
    QJsonObject out;
    for (QWidget *w : m_paramHost->findChildren<QWidget *>())
    {
        const QString key = QtFieldRenderer::keyOf(w);
        if (key.isEmpty())
            continue;
        const QVariant value = QtFieldRenderer::valueOf(w);
        // 空字符串不传，让解析器走内置默认（如 database_path 留空用内置模板）。
        if (value.typeId() == QMetaType::QString && value.toString().isEmpty())
            continue;
        out.insert(key, QJsonValue::fromVariant(value));
    }
    return out;
}

void ProtocolInspectorDialog::chooseFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择字节文件"));
    if (!path.isEmpty())
        m_path->setText(path);
}

void ProtocolInspectorDialog::doParse()
{
    m_fields->clear();
    if (!m_current)
    {
        m_status->setText(QStringLiteral("请先选择协议"));
        return;
    }

    // 1) 取字节：优先粘贴文本，否则读文件。
    QByteArray bytes;
    const QString pasted = m_input->toPlainText();
    if (!pasted.trimmed().isEmpty())
    {
        bytes = pasted.toUtf8();
    }
    else
    {
        const QString path = m_path->text().trimmed();
        if (path.isEmpty())
        {
            m_status->setText(QStringLiteral("请选择文件或粘贴字节"));
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            m_status->setText(QStringLiteral("打开失败: %1").arg(file.errorString()));
            return;
        }
        bytes = file.readAll(); // 协议检查：一次只取一段；大文件按需扩展为分块
    }

    // 2) configure：只传自述 schema 收集的参数，不读全局。
    QString err;
    if (!m_current->configure(collectParams(), &err))
    {
        m_status->setText(QStringLiteral("配置失败: %1").arg(err));
        return;
    }

    // 3) parse → DecodedFrame → 字段树。
    nextpilot::plotscope::parser::DecodedFrame frame;
    if (!m_current->parse(bytes, frame))
    {
        m_status->setText(QStringLiteral("解析失败：不是有效的 %1 帧").arg(m_current->protocolName()));
        return;
    }

    auto *root = new QTreeWidgetItem(m_fields);
    root->setText(0, frame.frameName.isEmpty() ? frame.protocol : frame.frameName);
    root->setText(1, frame.frameKey);
    root->setExpanded(true);

    auto addChild = [root](const QString &k, const QString &v) {
        auto *it = new QTreeWidgetItem(root);
        it->setText(0, k);
        it->setText(2, v);
    };
    addChild(QStringLiteral("协议"), frame.protocol);
    addChild(QStringLiteral("帧键"), frame.frameKey);
    addChild(QStringLiteral("校验"), frame.checksumOk ? QStringLiteral("OK") : QStringLiteral("失败"));
    addChild(QStringLiteral("时间戳"), QString::number(frame.timestamp, 'f', 6));
    if (frame.unknown)
        addChild(QStringLiteral("注意"), QStringLiteral("帧型已识别但模板缺失，字段以原始形式展示"));

    for (const nextpilot::plotscope::parser::DecodedField &f : frame.fields)
    {
        auto *it = new QTreeWidgetItem(root);
        it->setText(0, f.name);
        it->setText(1, f.raw.toString());
        it->setText(2, f.value.toString());
        const QString unitDesc = f.unit.isEmpty() ? f.description : f.unit;
        it->setText(3, f.enumLabel.isEmpty() ? unitDesc : f.enumLabel);
    }

    m_fields->header()->resizeSection(0, 180);
    m_status->setText(QStringLiteral("已解析 %1 个字段").arg(frame.fields.size()));
}

void ProtocolInspectorDialog::run(nextpilot::plotscope::registry::ToolboxHub &registry, QWidget *parent)
{
    ProtocolInspectorDialog dlg(registry, parent);
    dlg.exec();
}

} // namespace nextpilot::plotscope::gui
