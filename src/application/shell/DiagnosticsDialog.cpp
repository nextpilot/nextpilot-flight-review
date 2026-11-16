#include "DiagnosticsDialog.h"
#include "ShellPaths.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace
{

QString extensionText(const QVector<ExtensionManager::ExtensionInfo> &extensions)
{
    QString text;
    QTextStream out(&text);
    for (const auto &entry : extensions)
    {
        const auto &d = entry.descriptor;
        out << d.id << "  " << d.name << "  " << d.version << "  "
            << (d.type == ps::components::ExtensionType::Builtin ? "builtin" : "dynamic")
            << "  " << (entry.active ? "active" : "inactive") << '\n';
        if (!entry.error.isEmpty())
            out << "  " << entry.error << '\n';
    }
    return text;
}
} // namespace

DiagnosticsDialog::DiagnosticsDialog(QWidget *parent, const QString &kernelInfo,
                                     const QVector<ExtensionManager::ExtensionInfo> &extensions,
                                     const QStringList &errors)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("关于 PlotScope"));
    resize(680, 480);

    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    auto *about = new QWidget(tabs);
    auto *aboutLay = new QVBoxLayout(about);
    auto *group = new QGroupBox(QStringLiteral("运行信息"), about);
    auto *form = new QFormLayout(group);
    form->addRow(QStringLiteral("应用"), new QLabel(QStringLiteral("PlotScope %1")
                                                       .arg(QCoreApplication::applicationVersion().isEmpty()
                                                                ? QStringLiteral("0.1.0")
                                                                : QCoreApplication::applicationVersion()), group));
    form->addRow(QStringLiteral("内核"), new QLabel(kernelInfo, group));
    form->addRow(QStringLiteral("配置目录"), new QLabel(ShellPaths::configDir(), group));
    form->addRow(QStringLiteral("日志目录"), new QLabel(ShellPaths::logDir(), group));
    aboutLay->addWidget(group);
    auto *buttons = new QHBoxLayout;
    auto *openConfig = new QPushButton(QStringLiteral("打开配置目录"), about);
    auto *openLog = new QPushButton(QStringLiteral("打开日志目录"), about);
    buttons->addWidget(openConfig);
    buttons->addWidget(openLog);
    buttons->addStretch(1);
    aboutLay->addLayout(buttons);
    aboutLay->addStretch(1);
    tabs->addTab(about, QStringLiteral("概览"));

    auto *extPage = new QWidget(tabs);
    auto *extLay = new QVBoxLayout(extPage);
    auto *extView = new QPlainTextEdit(extensionText(extensions), extPage);
    extView->setReadOnly(true);
    extView->setMaximumBlockCount(500);
    extLay->addWidget(extView);
    if (!errors.isEmpty())
    {
        auto *errorView = new QPlainTextEdit(errors.join(QLatin1Char('\n')), extPage);
        errorView->setReadOnly(true);
        extLay->addWidget(new QLabel(QStringLiteral("加载错误"), extPage));
        extLay->addWidget(errorView);
    }
    tabs->addTab(extPage, QStringLiteral("扩展"));
    root->addWidget(tabs, 1);

    auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = dialogButtons->addButton(QStringLiteral("复制信息"), QDialogButtonBox::ActionRole);
    root->addWidget(dialogButtons);

    connect(openConfig, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ShellPaths::configDir()));
    });
    connect(openLog, &QPushButton::clicked, this, [] {
        QDir().mkpath(ShellPaths::logDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(ShellPaths::logDir()));
    });
    connect(copy, &QPushButton::clicked, this, [kernelInfo, extensions, errors] {
        QString info = QStringLiteral("PlotScope %1\n%2\n")
                           .arg(QCoreApplication::applicationVersion().isEmpty()
                                    ? QStringLiteral("0.1.0")
                                    : QCoreApplication::applicationVersion(),
                                kernelInfo);
        info += extensionText(extensions);
        if (!errors.isEmpty())
            info += QStringLiteral("\n加载错误：\n") + errors.join(QLatin1Char('\n'));
        QApplication::clipboard()->setText(info);
    });
    connect(dialogButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
