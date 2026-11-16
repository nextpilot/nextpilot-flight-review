#include "DiagnosticsDialog.h"
#include "ShellPaths.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QProcess>
#include <QMessageBox>
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
            << "  " << (entry.enabled ? (entry.active ? "active" : "inactive") : "disabled") << '\n';
        if (!entry.error.isEmpty())
            out << "  " << entry.error << '\n';
    }
    return text;
}
} // namespace

DiagnosticsDialog::DiagnosticsDialog(QWidget *parent, const QString &kernelInfo,
                                     const QVector<ExtensionManager::ExtensionInfo> &extensions,
                                     const QStringList &errors, ExtensionManager *manager)
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
    auto *extList = new QListWidget(extPage);
    extList->setObjectName(QStringLiteral("extension.list"));
    for (const auto &entry : extensions)
    {
        const auto &d = entry.descriptor;
        QString state = entry.enabled ? (entry.active ? QStringLiteral("已启用") : QStringLiteral("未激活"))
                                      : QStringLiteral("已禁用");
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2  %3  %4  %5")
                .arg(d.id, d.name, d.version,
                     d.type == ps::components::ExtensionType::Builtin ? QStringLiteral("builtin")
                                                                     : QStringLiteral("dynamic"),
                     state),
            extList);
        item->setData(Qt::UserRole, d.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        if (!entry.error.isEmpty())
            item->setToolTip(entry.error);
    }
    extLay->addWidget(extList, 1);
    auto *hint = new QLabel(QStringLiteral("扩展启停写入配置，重启 PlotScope 后生效。"), extPage);
    hint->setStyleSheet(QStringLiteral("color:%1;").arg(ThemeManager::color(QStringLiteral("textDim")).name()));
    extLay->addWidget(hint);
    auto *restart = new QPushButton(QStringLiteral("重启应用"), extPage);
    extLay->addWidget(restart, 0, Qt::AlignRight);
    if (manager)
    {
        connect(extList, &QListWidget::itemChanged, this, [manager](QListWidgetItem *item) {
            manager->setEnabled(item->data(Qt::UserRole).toString(),
                                item->checkState() == Qt::Checked);
        });
        connect(restart, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, QStringLiteral("重启 PlotScope"),
                    QStringLiteral("扩展启停将在重启后生效，现在重启？")) != QMessageBox::Yes)
                return;
            const QStringList args = QCoreApplication::arguments();
            if (QProcess::startDetached(args.value(0), args.mid(1)))
                QCoreApplication::quit();
        });
    } else {
        restart->setEnabled(false);
    }
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
    dialogButtons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
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
