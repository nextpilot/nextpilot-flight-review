#include "CustomProtocolComponent.h"

#include "ProtocolEditorDialog.h"
#include "app/components/ProtocolEditorService.h"
#include "protocol/ProtocolSpec.h"
#include "ps_plugin_api.h"

#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

extern "C" const PsPluginDescriptor *ps_custom_protocol_loader(void);

namespace ps::components
{
namespace
{

QString protocolDir()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
             QStringLiteral("/protocols"));
    dir.mkpath(QStringLiteral("."));
    return dir.path();
}

QString nextProtocolPath()
{
    QDir dir(protocolDir());
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    for (int i = 0; i < 1000; ++i)
    {
        const QString path = dir.filePath(i == 0 ? QStringLiteral("new-%1.psproto").arg(stamp)
                                                 : QStringLiteral("new-%1-%2.psproto").arg(stamp).arg(i));
        if (!QFileInfo::exists(path))
            return path;
    }
    return dir.filePath(QStringLiteral("new-%1.psproto").arg(stamp));
}

} // namespace

ComponentManifest CustomProtocolComponent::manifest() const
{
    return {QStringLiteral("protocol.custom"), QStringLiteral("自定义协议"),
            QStringLiteral("1.0"), {QStringLiteral("loader"), QStringLiteral("protocol"),
                                    QStringLiteral("ribbon"), QStringLiteral("editor")}};
}

void CustomProtocolComponent::contribute(Registry &registry, const HostServices &host)
{
    DataSourceSpec customSource;
    customSource.descriptor = ps_custom_protocol_loader();
    customSource.id = QStringLiteral("data_loader_custom_protocol");
    customSource.label = QStringLiteral("自定义协议");
    customSource.transports = {QStringLiteral("file")};
    registry.addDataSource(customSource);

    ProtocolContribution protocol;
    protocol.kind = QStringLiteral("sequential");
    protocol.label = QStringLiteral("顺序段协议");
    protocol.validate = [](const QJsonObject &root) { return ps::protocol::validate(root); };
    protocol.blank = [] { return ProtocolEditorDialog::blankJsonText(); };
    protocol.createEditor = [](const QString &path, QWidget *parent) -> QDialog * {
        return new ProtocolEditorDialog(path, parent);
    };
    registry.addProtocol(protocol);
    ps::components::setProtocolEditorFactory([](const QString &path, QWidget *parent) -> QDialog * {
        return new ProtocolEditorDialog(path, parent);
    });

    registry.addRibbon(RibbonContribution{
        QStringLiteral("协议"), QStringLiteral("编辑器"),
        {{QStringLiteral("protocol.new"), QStringLiteral("新建协议"), {}, true},
         {QStringLiteral("protocol.open"), QStringLiteral("打开编辑"), {}, false},
         {QStringLiteral("protocol.validate"), QStringLiteral("校验协议"), {}, false}}});

    auto window = [host]() { return host.window ? host.window() : nullptr; };
    auto log = [host](const QString &text) {
        if (host.log)
            host.log(text);
    };

    registry.addAction(QStringLiteral("protocol.new"), [window, log](const ActionContext &ctx) {
        const QString path = nextProtocolPath();
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(ctx.parent ? ctx.parent : window(), QStringLiteral("新建协议"),
                                 QStringLiteral("无法写入：%1").arg(path));
            return;
        }
        f.write(ProtocolEditorDialog::blankJsonText().toUtf8());
        f.close();

        ProtocolEditorDialog dlg(path, ctx.parent ? ctx.parent : window());
        if (dlg.exec() == QDialog::Accepted)
            log(QStringLiteral("协议已创建：%1").arg(dlg.templatePath()));
    });

    registry.addAction(QStringLiteral("protocol.open"), [window, log](const ActionContext &ctx) {
        const QString path = QFileDialog::getOpenFileName(
            ctx.parent ? ctx.parent : window(), QStringLiteral("打开协议"), protocolDir(),
            QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
        if (path.isEmpty())
            return;
        ProtocolEditorDialog dlg(path, ctx.parent ? ctx.parent : window());
        if (dlg.exec() == QDialog::Accepted)
            log(QStringLiteral("协议已保存：%1").arg(dlg.templatePath()));
    });

    registry.addAction(QStringLiteral("protocol.validate"), [window, log](const ActionContext &ctx) {
        const QString path = QFileDialog::getOpenFileName(
            ctx.parent ? ctx.parent : window(), QStringLiteral("校验协议"), protocolDir(),
            QStringLiteral("PlotScope 协议 (*.psproto);;JSON (*.json)"));
        if (path.isEmpty())
            return;
        const QStringList errs = ProtocolEditorDialog::validateFile(path);
        if (errs.isEmpty())
        {
            QMessageBox::information(ctx.parent ? ctx.parent : window(), QStringLiteral("协议校验"),
                                     QStringLiteral("校验通过：%1").arg(QFileInfo(path).fileName()));
            log(QStringLiteral("协议校验通过：%1").arg(path));
            return;
        }
        QMessageBox box(QMessageBox::Warning, QStringLiteral("协议校验"),
                        QStringLiteral("发现 %1 项问题").arg(errs.size()),
                        QMessageBox::Ok, ctx.parent ? ctx.parent : window());
        box.setDetailedText(errs.join(QStringLiteral("\n")));
        box.exec();
        log(QStringLiteral("协议校验失败：%1 项 %2").arg(errs.size()).arg(path));
    });
}

} // namespace ps::components
