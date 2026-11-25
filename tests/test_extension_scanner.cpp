// A8b-M1 ExtensionScanner 单测：扫目录读 manifest.json，只登记 contributes。
// 用 QTemporaryDir 造样例，不依赖构建产物目录。

#include "ExtensionScanner.h"
#include "ToolboxHub.h"
#include "RegistryTree.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QString>

#include <cstdio>

using namespace nextpilot::plotscope::registry;

namespace
{
int g_failed = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)

void writeManifest(const QString &extDir, const QByteArray &json)
{
    QDir().mkpath(extDir);
    QFile f(extDir + QStringLiteral("/manifest.json"));
    f.open(QIODevice::WriteOnly);
    f.write(json);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString root = tmp.path();

    // 正常扩展
    writeManifest(root + QStringLiteral("/plotscope.hello"),
                  R"({"id":"plotscope.hello","version":"0.1.0","contributes":{
                      "commands":[{"command":"hello.say","title":"说你好"}],
                      "menus":{"commandPalette":[{"command":"hello.say"}]}}})");
    // 坏输入 1：manifest JSON 非法
    writeManifest(root + QStringLiteral("/bad.json"), R"({not json})");
    // 坏输入 2：缺 contributes
    writeManifest(root + QStringLiteral("/bad.noctb"), R"({"id":"bad.noctb"})");
    // 坏输入 3：缺 id
    writeManifest(root + QStringLiteral("/bad.noid"),
                  R"({"contributes":{"commands":[{"command":"x"}]}})");
    // 非扩展目录（无 manifest）
    QDir().mkpath(root + QStringLiteral("/not_an_ext"));

    ExtensionScanReport report = scanExtensions(root);
    CHECK(report.docs.size() == 1);       // 只有正常扩展被解析
    CHECK(report.docs.at(0).owner == QLatin1String("plotscope.hello"));
    CHECK(report.docs.at(0).commands.size() == 1);
    CHECK(report.docs.at(0).commandPalette.size() == 1);
    CHECK(report.scannedDirs.size() == 4); // 4 个含 manifest 的目录被扫
    CHECK(report.issues.size() == 3);      // 3 个坏输入各一条

    // mount 后扩展表可读，验证扫描→贡献面链路
    RegistryTree tree;
    ToolboxHub extensions(tree);
    for (const auto &doc : report.docs)
        extensions.mountContribution(doc);
    CHECK(extensions.contributedCommands().size() == 1);
    CHECK(extensions.findCommand(QStringLiteral("hello.say")) != nullptr);
    CHECK(extensions.paletteEntries().size() == 1);

    // 空根（零扩展底座）不崩、无 issue
    QTemporaryDir empty;
    ExtensionScanReport emptyReport = scanExtensions(empty.path());
    CHECK(emptyReport.docs.isEmpty());
    CHECK(emptyReport.issues.isEmpty());

    if (g_failed == 0)
        std::printf("extension_scanner: ALL PASS\n");
    else
        std::printf("extension_scanner: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
