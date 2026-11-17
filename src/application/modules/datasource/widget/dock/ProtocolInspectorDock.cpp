#include "ProtocolInspectorDock.h"

#include "datasource/MessageParserRegistry.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

namespace {

constexpr int kMaxFrames = 10000;

QString valueText(const PsDecodedField &field)
{
    if (field.value.isValid() && !field.value.toString().isEmpty())
        return field.value.toString();
    if (field.raw.isValid() && !field.raw.toString().isEmpty())
        return field.raw.toString();
    return {};
}

} // namespace

ProtocolInspectorDock::ProtocolInspectorDock(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(4);
    m_protocol = new QComboBox(this);
    m_protocol->setMinimumWidth(150);
    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(QStringLiteral("选择日志或协议文件"));
    auto *browse = new QPushButton(QStringLiteral("浏览"), this);
    auto *reload = new QPushButton(QStringLiteral("解析"), this);
    bar->addWidget(new QLabel(QStringLiteral("协议"), this));
    bar->addWidget(m_protocol);
    bar->addWidget(m_path, 1);
    bar->addWidget(browse);
    bar->addWidget(reload);
    root->addLayout(bar);

    m_status = new QLabel(QStringLiteral("未加载"), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *split = new QSplitter(Qt::Vertical, this);
    m_frames = new QTreeWidget(split);
    m_frames->setColumnCount(5);
    m_frames->setHeaderLabels({QStringLiteral("序号"), QStringLiteral("帧"), QStringLiteral("Key"),
                               QStringLiteral("大小"), QStringLiteral("状态")});
    m_frames->setRootIsDecorated(false);
    m_frames->setAlternatingRowColors(false);
    m_frames->header()->setStretchLastSection(false);
    m_frames->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_frames->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_frames->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_frames->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_frames->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    m_fields = new QTableWidget(split);
    m_fields->setColumnCount(6);
    m_fields->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("值"),
                                         QStringLiteral("单位"), QStringLiteral("枚举"),
                                         QStringLiteral("偏移"), QStringLiteral("说明")});
    m_fields->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fields->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fields->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fields->verticalHeader()->setVisible(false);
    m_fields->horizontalHeader()->setStretchLastSection(true);
    m_fields->setColumnWidth(0, 180);
    m_fields->setColumnWidth(1, 220);
    m_fields->setColumnWidth(2, 70);
    m_fields->setColumnWidth(3, 150);
    m_fields->setColumnWidth(4, 70);

    split->addWidget(m_frames);
    split->addWidget(m_fields);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 5);
    root->addWidget(split, 1);

    refreshProtocols();
    connect(browse, &QPushButton::clicked, this, &ProtocolInspectorDock::browse);
    connect(reload, &QPushButton::clicked, this, &ProtocolInspectorDock::reload);
    connect(m_frames, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (item)
                    showFrame(item->data(0, Qt::UserRole).toInt());
            });
}

void ProtocolInspectorDock::refreshProtocols()
{
    const QString current = m_protocol->currentData().toString();
    m_protocol->clear();
    for (const MessageParserSpec &spec : MessageParserRegistry::instance().all())
        m_protocol->addItem(spec.displayName.isEmpty() ? spec.protocolId : spec.displayName,
                            spec.protocolId);
    const int index = m_protocol->findData(current);
    if (index >= 0)
        m_protocol->setCurrentIndex(index);
}

void ProtocolInspectorDock::browse()
{
    QStringList filters;
    for (const MessageParserSpec &spec : MessageParserRegistry::instance().all())
    {
        if (spec.fileExtensions.isEmpty())
            continue;
        QStringList globs;
        for (const QString &ext : spec.fileExtensions)
            globs.append(QStringLiteral("*%1").arg(ext));
        filters.append(QStringLiteral("%1 (%2)").arg(spec.displayName, globs.join(QStringLiteral(" "))));
    }
    filters.append(QStringLiteral("全部 (*)"));
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("协议检查"), {},
                                                      filters.join(QStringLiteral(";;")));
    if (!path.isEmpty())
        loadFile(path);
}

void ProtocolInspectorDock::reload()
{
    if (!m_path->text().isEmpty())
        loadFile(m_path->text());
}

QString ProtocolInspectorDock::chooseProtocol(const QString &path, const QByteArray &head) const
{
    const QString suffix = QFileInfo(path).suffix();
    const auto byExt = MessageParserRegistry::instance().findByExtension(suffix);
    if (byExt)
        return byExt->protocolId;
    const auto bySniff = MessageParserRegistry::instance().findBySniff(head);
    if (bySniff)
        return bySniff->protocolId;
    return m_protocol->currentData().toString();
}

bool ProtocolInspectorDock::loadFile(const QString &path, const QString &protocolId)
{
    m_path->setText(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        m_status->setText(QStringLiteral("无法打开：%1").arg(path));
        return false;
    }
    const QByteArray data = file.readAll();
    const QByteArray head = data.left(64);
    QString chosen = protocolId;
    if (chosen.isEmpty())
        chosen = chooseProtocol(path, head);
    if (chosen.isEmpty())
    {
        m_status->setText(QStringLiteral("没有找到匹配的 MessageParser：%1").arg(path));
        return false;
    }
    const int comboIndex = m_protocol->findData(chosen);
    if (comboIndex >= 0)
        m_protocol->setCurrentIndex(comboIndex);

    std::unique_ptr<IMessageParser> parser = MessageParserRegistry::instance().create(chosen);
    if (!parser)
    {
        m_status->setText(QStringLiteral("协议 parser 未注册：%1").arg(chosen));
        return false;
    }

    m_frames->clear();
    m_fields->setRowCount(0);
    m_frameData.clear();
    m_lastPath = path;

    QByteArray rest = data;
    int parseErrors = 0;
    while (!rest.isEmpty() && m_frameData.size() < kMaxFrames)
    {
        PsDecodedFrame frame;
        if (!parser->parse(rest, frame) || frame.rawBytes.isEmpty())
        {
            ++parseErrors;
            break;
        }
        const int at = rest.indexOf(frame.rawBytes);
        if (at < 0)
        {
            ++parseErrors;
            break;
        }
        m_frameData.append(frame);
        auto *item = new QTreeWidgetItem(m_frames);
        const int index = m_frameData.size() - 1;
        item->setText(0, QString::number(index + 1));
        item->setText(1, frame.frameName);
        item->setText(2, frame.frameKey);
        item->setText(3, QString::number(frame.rawBytes.size()));
        item->setText(4, frame.unknown ? QStringLiteral("未知")
                                      : (frame.checksumOk ? QStringLiteral("OK")
                                                          : QStringLiteral("校验失败")));
        item->setData(0, Qt::UserRole, index);
        rest.remove(0, at + frame.rawBytes.size());
    }

    if (m_frameData.isEmpty())
    {
        m_status->setText(QStringLiteral("没有解析到帧：%1").arg(path));
        return false;
    }
    m_frames->setCurrentItem(m_frames->topLevelItem(0));
    showFrame(0);
    const int remain = rest.size();
    m_status->setText(QStringLiteral("已解析 %1 帧%2%3")
                          .arg(m_frameData.size())
                          .arg(remain > 0 ? QStringLiteral("，剩余 %1 字节").arg(remain) : QString())
                          .arg(parseErrors ? QStringLiteral("（有截断/无法继续的帧）") : QString()));
    return true;
}

void ProtocolInspectorDock::selectFrame(int index)
{
    if (index < 0 || index >= m_frames->topLevelItemCount())
        return;
    m_frames->setCurrentItem(m_frames->topLevelItem(index));
    showFrame(index);
}

void ProtocolInspectorDock::showFrame(int index)
{
    if (index < 0 || index >= m_frameData.size())
        return;
    const PsDecodedFrame &frame = m_frameData.at(index);
    m_fields->setRowCount(frame.fields.size());
    for (int row = 0; row < frame.fields.size(); ++row)
    {
        const PsDecodedField &field = frame.fields.at(row);
        m_fields->setItem(row, 0, new QTableWidgetItem(field.name));
        m_fields->setItem(row, 1, new QTableWidgetItem(valueText(field)));
        m_fields->setItem(row, 2, new QTableWidgetItem(field.unit));
        m_fields->setItem(row, 3, new QTableWidgetItem(field.enumLabel));
        m_fields->setItem(row, 4, new QTableWidgetItem(field.offset >= 0 ? QString::number(field.offset)
                                                                        : QStringLiteral("-")));
        m_fields->setItem(row, 5, new QTableWidgetItem(field.description));
    }
}
