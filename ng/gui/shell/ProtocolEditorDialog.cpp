#include "ProtocolEditorDialog.h"

#include "ThemeManager.h"

#include "protocol/ProtocolDocument.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/SequentialCodec.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{
namespace
{
// 把 "AA 55 0C 00" / "AA550C00" 形式的 hex 转成字节。
QByteArray parseHex(const QString &text)
{
    QString clean;
    for (QChar c : text)
    {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char('|'))
            continue;
        clean.append(c.toUpper());
    }
    if (clean.size() % 2 != 0)
        return QByteArray();
    QByteArray out;
    for (int i = 0; i < clean.size(); i += 2)
    {
        bool ok = false;
        const int b = clean.mid(i, 2).toInt(&ok, 16);
        if (!ok)
            return QByteArray();
        out.append(static_cast<char>(b));
    }
    return out;
}

QString protocolsDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                      + QStringLiteral("/protocols");
    return dir;
}
} // namespace

ProtocolEditorDialog::ProtocolEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("协议编辑器 · 顺序段协议"));
    resize(1100, 720);

    // --- 顶部：文件操作 ---
    auto *top = new QHBoxLayout;
    auto *newBtn = new QPushButton(QStringLiteral("新建"));
    auto *openBtn = new QPushButton(QStringLiteral("打开"));
    m_saveBtn = new QPushButton(QStringLiteral("保存"));
    auto *validateBtn = new QPushButton(QStringLiteral("校验"));
    top->addWidget(newBtn);
    top->addWidget(openBtn);
    top->addWidget(m_saveBtn);
    top->addWidget(validateBtn);
    top->addStretch(1);

    // --- 主体：左 JSON 编辑，右 校验/试算 ---
    auto *body = new QSplitter(Qt::Horizontal);

    auto *left = new QWidget;
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->addWidget(new QLabel(QStringLiteral("协议模板 JSON")));
    m_json = new QPlainTextEdit;
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_json->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    leftLay->addWidget(m_json, 1);

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
    rightLay->addWidget(m_preview, 1);

    body->addWidget(left);
    body->addWidget(right);
    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 2);

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

    newDoc();
    applyTheme();
}

ProtocolEditorDialog::~ProtocolEditorDialog() = default;

QString ProtocolEditorDialog::blankTemplate()
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
                  const QString &role) {
        QJsonObject s;
        s.insert(QStringLiteral("name"), name);
        s.insert(QStringLiteral("size"), size);
        s.insert(QStringLiteral("type"), type);
        s.insert(QStringLiteral("role"), role);
        return s;
    };
    segs.append(seg(QStringLiteral("sync1"), 1, QStringLiteral("u8"), QStringLiteral("sync")));
    segs.append(seg(QStringLiteral("a"), 1, QStringLiteral("u16"), QStringLiteral("data")));
    segs.append(seg(QStringLiteral("b"), 1, QStringLiteral("u16"), QStringLiteral("data")));
    o.insert(QStringLiteral("segments"), segs);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void ProtocolEditorDialog::newDoc()
{
    m_path.clear();
    m_json->setPlainText(blankTemplate());
    m_status->setText(QStringLiteral("新建协议（未保存）"));
    m_validation->setText(QStringLiteral("尚未校验"));
    m_preview->clear();
}

void ProtocolEditorDialog::openDoc()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开协议"), protocolsDir(),
        QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, QStringLiteral("打开协议"),
                             QStringLiteral("无法读取：%1").arg(path));
        return;
    }
    m_json->setPlainText(QString::fromUtf8(f.readAll()));
    m_path = path;
    m_status->setText(QStringLiteral("已打开：%1").arg(QFileInfo(path).fileName()));
    validateDoc();
}

void ProtocolEditorDialog::saveDoc()
{
    QString path = m_path;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存协议"), protocolsDir() + QStringLiteral("/new.psproto"),
            QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
        if (path.isEmpty())
            return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, QStringLiteral("保存协议"),
                             QStringLiteral("无法写入：%1").arg(path));
        return;
    }
    f.write(m_json->toPlainText().toUtf8());
    m_path = path;
    m_status->setText(QStringLiteral("已保存：%1").arg(path));
}

void ProtocolEditorDialog::validateDoc()
{
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &pe).object();
    if (pe.error != QJsonParseError::NoError)
    {
        m_validation->setText(QStringLiteral("JSON 解析失败：%1").arg(pe.errorString()));
        return;
    }
    const QStringList issues = nextpilot::plotscope::protocol::validate(root);
    nextpilot::plotscope::protocol::ProtocolDocument doc;
    std::string err;
    if (!nextpilot::plotscope::protocol::parseDocument(root, doc, err))
        m_validation->setText(QStringLiteral("校验失败：%1").arg(QString::fromStdString(err)));
    else if (issues.isEmpty())
        m_validation->setText(QStringLiteral("✓ 校验通过（%1 段）").arg(doc.segments.size()));
    else
        m_validation->setText(QStringLiteral("校验发现 %1 项：\n%2")
                                  .arg(issues.size()).arg(issues.join(QStringLiteral("\n"))));
}

void ProtocolEditorDialog::previewFrame()
{
    const QByteArray frame = parseHex(m_hex->toPlainText());
    m_preview->clear();
    if (frame.isEmpty())
    {
        m_status->setText(QStringLiteral("样例帧为空或 hex 非法"));
        return;
    }
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(m_json->toPlainText().toUtf8(), &pe).object();
    nextpilot::plotscope::protocol::ProtocolDocument doc;
    std::string err;
    if (!nextpilot::plotscope::protocol::parseDocument(root, doc, err))
    {
        m_status->setText(QStringLiteral("协议模板无效，无法试算"));
        return;
    }
    const auto preview = nextpilot::plotscope::protocol::previewSequentialFrame(frame, doc);
    if (!preview.ok)
    {
        m_status->setText(QStringLiteral("试算失败：%1").arg(preview.error));
        return;
    }
    for (const QString &line : preview.lines)
    {
        const int colon = line.indexOf(QLatin1Char(':'));
        auto *item = new QTreeWidgetItem(m_preview);
        if (colon > 0)
        {
            item->setText(0, line.left(colon).trimmed());
            item->setText(1, line.mid(colon + 1).trimmed());
        }
        else
        {
            item->setText(0, line);
        }
    }
    m_status->setText(QStringLiteral("试算完成：%1 项").arg(preview.lines.size()));
}

void ProtocolEditorDialog::applyTheme()
{
    // 对话框走全局 QSS/palette（ThemeManager 已设），这里不写死任何色值。
    setStyleSheet(QString());
}

void ProtocolEditorDialog::run(QWidget *parent)
{
    ProtocolEditorDialog dlg(parent);
    dlg.exec();
}

} // namespace nextpilot::plotscope::gui
