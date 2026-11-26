// A8b-M1 e2e cmd-ext：内置测试扩展一条命令 → 接到命令面板 → 执行成功 → 卸载撤回。
//
// 走与 gui_main 相同的装配路径：扩展 contributes（来自扫描/manifest）→
// assembly::ExtensionActivationService.bind(CommandRegistry*, RuntimeHost&) → host.execute(intent)。
// 用真实 RuntimeHost 与 CommandRegistry，不 mock；不构造完整 MainWindow（保持测试稳定）。

#include "App.h"
#include "AppContext.h"
#include "RuntimeHost.h"
#include "ExtensionActivationService.h"
#include "command/CommandRegistry.h"
#include "ContributionParser.h"
#include "ContributionsFormat.h"
#include "ViewRegistry.h"
#include "IViewsService.h"
#include "IEditorService.h"
#include "IEditorProvider.h"
#include "ResourceRef.h"
#include "IActionProvider.h"
#include "Intent.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdio>

using namespace nextpilot::plotscope;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::command;
using namespace nextpilot::plotscope::intent;

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

// A8b-M3-a 第 2 笔：假 "web" provider（§9 h 判据）——壳不改一行即可打开网页页签。
class FakeWebProvider final : public nextpilot::plotscope::registry::IEditorProvider
{
public:
    bool canOpen(const nextpilot::plotscope::registry::ResourceRef &ref) const override
    {
        return ref.mime == QLatin1String("text/html");
    }
    QWidget *createEditor(const nextpilot::plotscope::registry::ResourceRef &,
                          QWidget *parent) override
    {
        return new QWidget(parent);
    }
};
FakeWebProvider g_webProvider;
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 组合根（headless）：App 自带 AppContext + RuntimeHost。
    app::App application; // 默认空 settingsPath：内存态
    application.startup(/*headless=*/true);

    // 样例扩展的 contributes（与 tests/e2e/cmd-ext/extensions 的 manifest 等价）。
    const char *manifest = R"({
        "commands":[{"command":"hello.say","title":"说你好","category":"测试","order":10}],
        "menus":{
            "commandPalette":[{"command":"hello.say","group":"z@1"}],
            "view/title":[{"command":"hello.say","group":"navigation","when":"view == hello.view"}],
            "view/item/context":[{"command":"hello.say","group":"inline","when":"view == hello.view && item.kind == demo"}]
        },
        "viewsContainers":[{"id":"hello.container","title":"示例视图","area":"sidebar","order":20}],
        "views":[{"id":"hello.view","name":"示例","container":"hello.container","icon":"hello.icon","order":0},
                 {"id":"hello.view.plain","name":"无图标示例","container":"hello.container","order":1}],
        "customEditors":[{"viewType":"web","displayName":"网页","priority":5,
                          "selector":[{"mime":"text/html"}]}],
        "configuration":[{"title":"示例设置","iconId":"chat","order":40,
                          "properties":{
                            "hello.greeting":{"type":"string","default":"你好","description":"问候语","order":1},
                            "hello.count":{"type":"number","default":3,"minimum":1,"maximum":10,"order":2},
                            "hello.verbose":{"type":"boolean","default":false,"order":3}
                          }}],
        "colors":{"hello.accent":{"dark":"#4fc3f7","light":"#0277bd"}},
        "statusbar":[{"id":"hello.status","side":"right","priority":30,"text":"示例: --","commandId":"hello.say"}]
    })";
    auto obj = QJsonDocument::fromJson(QByteArray(manifest)).object();
    ContributesDoc doc = parseContributes(obj, QStringLiteral("plotscope.helloworld")).doc;
    CHECK(application.context().toolbox().mountContribution(doc));

    // 装配层同款接线：激活服务把贡献命令灌进一个命令表 + 注册执行 handler。
    CommandRegistry commands;
    assembly::ExtensionActivationService activation(application.context());
    activation.bind(&commands, application.host());

    // 面板出现：贡献命令已进命令表（buildCommandItems 从 commands() 读）。
    CHECK(activation.boundCommandCount() == 2); // hello.say + workbench.action.openSettings（core.settings）
    CHECK(commands.contains(QStringLiteral("hello.say")));
    CHECK(commands.contains(QStringLiteral("workbench.action.openSettings")));
    const CommandDescriptor *d = commands.find(QStringLiteral("hello.say"));
    CHECK(d != nullptr);
    if (d)
    {
        CHECK(d->title == QStringLiteral("说你好"));
        CHECK(d->category == QStringLiteral("测试"));
        CHECK(d->intentOp == QLatin1String("hello.say"));
        CHECK(d->provider == QLatin1String("plotscope.helloworld"));
    }

    // A8b-M2 e2e 视图用例：contributes 声明经唯一扩展注册表可读，侧栏容器/视图/视图标题动作
    // /右键项/colors/statusbar 都在；ViewRegistry 能按容器查视图并挂 provider。
    auto &ext = application.context().toolbox();
    CHECK(ext.contributedViewContainers().size() == 1);
    CHECK(ext.contributedViewContainers().at(0).item.id == QLatin1String("hello.container"));
    CHECK(ext.contributedViewContainers().at(0).item.area == QLatin1String("sidebar"));
    CHECK(ext.contributedViews().size() == 2);
    CHECK(ext.contributedViews().at(0).item.container == QLatin1String("hello.container"));
    // icon 原样进贡献表：首个视图 icon=hello.icon，第二个未声明 icon 为空。
    CHECK(ext.contributedViews().at(0).item.icon == QLatin1String("hello.icon"));
    CHECK(ext.contributedViews().at(1).item.icon.isEmpty());
    // view/title 与 view/item/context 两个 place 的菜单项可见（侧栏视图的动作）。
    const auto titleItems = ext.menuItemsIn(QStringLiteral("view/title"));
    CHECK(titleItems.size() == 1);
    CHECK(titleItems.at(0).item.command == QLatin1String("hello.say"));
    CHECK(ext.menuItemsIn(QStringLiteral("view/item/context")).size() == 1);
    CHECK(ext.contributedColors().size() == 1);
    CHECK(ext.contributedColors().at(0).item.light == QLatin1String("#0277bd"));
    CHECK(ext.contributedStatuses().size() == 1);
    CHECK(ext.contributedStatuses().at(0).item.id == QLatin1String("hello.status"));

    // A8b-M2 裁定 52442006 Step 5 e2e：bind 已把扩展包 views 声明灌进组合根的
    // ViewRegistry（views.registry 服务），壳读回走 IViewsService（views.readback）。
    auto *services = application.host().services();
    CHECK(services != nullptr);
    auto *viewSvc =
        services ? services->getService<nextpilot::plotscope::workbench::IViewsService>(
                       QStringLiteral("views.readback"))
                 : nullptr;
    CHECK(viewSvc != nullptr);
    if (viewSvc)
    {
        const auto containers = viewSvc->containersIn(QStringLiteral("sidebar"));
        CHECK(containers.size() == 1);
        if (!containers.isEmpty())
        {
            CHECK(containers.at(0).id == QLatin1String("hello.container"));
            CHECK(containers.at(0).title == QStringLiteral("示例视图"));
        }
        const auto inContainer = viewSvc->viewsInContainer(QStringLiteral("hello.container"));
        CHECK(inContainer.size() == 2);
        if (!inContainer.isEmpty())
            CHECK(inContainer.at(0).id == QLatin1String("hello.view"));
        const auto one = viewSvc->viewForId(QStringLiteral("hello.view"));
        CHECK(one.owner == QLatin1String("plotscope.helloworld"));
        CHECK(viewSvc->viewForId(QStringLiteral("nope")).owner.isEmpty());

        // A2b 笔 2a icon：声明的 icon 原样回读；未声明的视图 icon 为空（CLI 输出时为 "-"）。
        CHECK(one.icon == QLatin1String("hello.icon"));
        const auto plain = viewSvc->viewForId(QStringLiteral("hello.view.plain"));
        CHECK(plain.owner == QLatin1String("plotscope.helloworld"));
        CHECK(plain.icon.isEmpty());
        // icon 行格式由生产侧 formatContributionViewLine 唯一定义，CLI 与本测试共用；
        // 整行精确相等（非 contains）：owner/container/order/icon 任一打错都会红。
        CHECK(
            formatContributionViewLine(one.owner, one) ==
            QStringLiteral("plotscope.helloworld views hello.view container=hello.container order=0 icon=hello.icon"));
        CHECK(formatContributionViewLine(plain.owner, plain) ==
              QStringLiteral("plotscope.helloworld views hello.view.plain container=hello.container order=1 icon=-"));
    }

    // viewId 注册内容工厂后 openView 走 ViewRegistry::create 同一条链（这里工厂
    // headless 返回 nullptr，只验证链路命中 provider）。
    ViewRegistry *viewsReg =
        services ? services->getService<ViewRegistry>(QStringLiteral("views.registry")) : nullptr;
    CHECK(viewsReg != nullptr);
    if (viewsReg)
    {
        CHECK(viewsReg->viewsIn(QStringLiteral("hello.container")).size() == 2);
        CHECK(viewsReg->registerViewProvider(
            QStringLiteral("hello.view"),
            [](context::AppContext &, intent::IIntentExecutor &, QWidget *) -> QWidget * {
                return nullptr;
            },
            QStringLiteral("plotscope.helloworld")));
        CHECK(viewsReg->provider(QStringLiteral("hello.view")) != nullptr);
        CHECK(viewSvc->openView(QStringLiteral("hello.view"), application.context(),
                                application.host(), nullptr) == nullptr);
    }

    // ---- A8b-M3-a 第 2 笔 e2e：customEditors + registerEditorProvider + 四语义链路 ----
    using nextpilot::plotscope::workbench::IEditorService;
    using nextpilot::plotscope::registry::ResourceRef;
    CHECK(ext.contributedCustomEditors().size() == 1);
    CHECK(ext.contributedCustomEditors().at(0).item.viewType == QLatin1String("web"));
    // e2e：manifest configuration 解析进同一张贡献表（属性数/默认值/scope）。
    // 注：core.settings 先于本 owner 挂载，旧对话框的设置项已迁进它的四个分组，按 owner 查。
    {
        // contributedConfigurations() 按值返回 QList：直接在 range-for 里取 &c，指针指向临时
        // 容器，循环结束即析构。ASan 报 heap-use-after-free；macOS 上则表现为这里 4 条断言
        // 读到垃圾值（同一处 bug、不同分配器不同表现）。先把列表落到局部变量再取地址。
        const auto helloCfgs = ext.contributedConfigurations();
        const ToolboxHub::OwnedConfiguration *helloCfg = nullptr;
        for (const auto &c : helloCfgs)
            if (c.owner == QLatin1String("plotscope.helloworld"))
            {
                helloCfg = &c;
                break;
            }
        CHECK(helloCfg != nullptr);
        int coreCfgGroups = 0;
        QStringList coreCfgIds;
        QSet<QString> coreCfgKeys;
        for (const auto &c : ext.contributedConfigurations())
        {
            if (c.owner != QLatin1String("core.settings"))
                continue;
            ++coreCfgGroups;
            coreCfgIds.append(c.item.id);
            for (const auto &p : c.item.properties)
                coreCfgKeys.insert(p.key);
        }
        // hello 1 组 + core.settings 5 组（外观/通用/时间轴/数据/导出，原对话框设置项已迁入）。
        CHECK(ext.contributedConfigurations().size() == 6);
        CHECK(coreCfgGroups == 5);
        CHECK(coreCfgIds.contains(QStringLiteral("core.appearance")));
        CHECK(coreCfgIds.contains(QStringLiteral("core.general")));
        CHECK(coreCfgIds.contains(QStringLiteral("core.timeline")));
        CHECK(coreCfgIds.contains(QStringLiteral("core.data")));
        CHECK(coreCfgIds.contains(QStringLiteral("core.export")));
        // 迁移的五条键必须在声明面可达（旧 PreferencesDialog 曾承载、删壳不能丢）。
        for (const QString &k :
             {QStringLiteral("theme"), QStringLiteral("startup.recovery"), QStringLiteral("export.csv.maxPoints"),
              QStringLiteral("derive.maxPoints"), QStringLiteral("plot.time.format")})
            CHECK(coreCfgKeys.contains(k));
        if (helloCfg)
        {
            // 平台诊断：只在断言将失败时打印实际值。macOS 首轮这里红了，日志里只有断言文本，
            // 看不出是编码、顺序还是解析本身的问题。
            if (helloCfg->item.title != QStringLiteral("示例设置") || helloCfg->item.properties.size() != 3)
            {
                std::printf("[diag] hello.title=[%s] props=%d\n", qPrintable(helloCfg->item.title),
                            int(helloCfg->item.properties.size()));
                for (const auto &p : helloCfg->item.properties)
                    std::printf("[diag] key=[%s] type=[%s] default=[%s] scope=[%s]\n", qPrintable(p.key),
                                qPrintable(p.type), qPrintable(p.defaultValue.toString()), qPrintable(p.scope));
            }
            CHECK(helloCfg->item.title == QStringLiteral("示例设置"));
            CHECK(helloCfg->item.properties.size() == 3);
            bool greetingOk = false;
            for (const auto &p : helloCfg->item.properties)
                if (p.key == QLatin1String("hello.greeting"))
                    greetingOk = p.type == QLatin1String("string") &&
                                 p.defaultValue.toString() == QStringLiteral("你好") &&
                                 p.scope == QLatin1String("window");
            CHECK(greetingOk);
            bool countOk = false;
            for (const auto &p : helloCfg->item.properties)
                if (p.key == QLatin1String("hello.count"))
                    countOk = p.hasMinimum && p.minimum == 1.0 && p.hasMaximum && p.maximum == 10.0;
            CHECK(countOk);
        }
    }
    auto *editorSvc = services ? services->getService<IEditorService>(
                                     QStringLiteral("editor.readback"))
                               : nullptr;
    CHECK(editorSvc != nullptr);
    ResourceRef html{QStringLiteral("https://example.com"), QStringLiteral("text/html"),
                     QStringLiteral("示例页")};
    ResourceRef txt{QStringLiteral("/tmp/a.txt"), QStringLiteral("text/plain"), QStringLiteral("备注")};
    if (editorSvc && viewsReg)
    {
        // 声明随 bind 灌进同一张 ViewRegistry；读回含扩展 web + 内置 text（core.text）
        // + 内置 settings（core.settings）。
        CHECK(editorSvc->customEditors().size() == 3);
        bool settingsDeclared = false;
        for (const auto &ed : editorSvc->customEditors())
            if (ed.viewType == QLatin1String("workbench.editor.settings2"))
                settingsDeclared = true;
        CHECK(settingsDeclared);
        // 防冒名：owner 不等于声明者 → 拒。
        CHECK(viewsReg->registerEditorProvider(QStringLiteral("web"), &g_webProvider,
                                               QStringLiteral("evil")) == nullptr);
        // 防冒名（M3-b）：settings 的声明者只能是 core.settings。
        CHECK(viewsReg->registerEditorProvider(QStringLiteral("workbench.editor.settings2"), &g_webProvider,
                                               QStringLiteral("evil")) == nullptr);
        // 声明者注册 → 成功，返回可撤销句柄（editorViewTypeFor 要求 provider 在位，先注册再匹配）。
        auto webDispose = viewsReg->registerEditorProvider(
            QStringLiteral("web"), &g_webProvider, QStringLiteral("plotscope.helloworld"));
        CHECK(webDispose != nullptr);
        // 资源匹配：mime text/html → 扩展的 web；text/plain → 内置 text。
        CHECK(editorSvc->editorViewTypeFor(html) == QLatin1String("web"));
        CHECK(editorSvc->editorViewTypeFor(txt) == QLatin1String("text"));
        // 壳不改一行：openEditor("web", html) 命中假 provider，返回不透明控件。
        QWidget *webPage =
            editorSvc->openEditor(QStringLiteral("web"), html, application.context(),
                                  application.host(), nullptr);
        CHECK(webPage != nullptr);
        delete webPage;
        // 内置 text 编辑器零扩展可用：打开 → 控件；保存 → 本地文件可写校验。
        QWidget *textPage = editorSvc->openEditor(QStringLiteral("text"), txt,
                                                  application.context(), application.host(),
                                                  nullptr);
        CHECK(textPage != nullptr);
        delete textPage;
        CHECK(editorSvc->providerFor(QStringLiteral("text")) != nullptr);
        // saveAs 落盘（3a-4）：title=源 uri，uri=另存目标；写入内容与源一致。
        {
            const QString srcPath = QDir::temp().filePath(QStringLiteral("ps_saveas_src.txt"));
            const QString dstPath = QDir::temp().filePath(QStringLiteral("ps_saveas_dst.txt"));
            QFile srcFile(srcPath);
            CHECK(srcFile.open(QIODevice::WriteOnly | QIODevice::Text));
            srcFile.write("hello saveAs\n");
            srcFile.close();
            auto *textProv = editorSvc->providerFor(QStringLiteral("text"));
            CHECK(textProv != nullptr);
            ResourceRef target{dstPath, QStringLiteral("text/plain"), srcPath};
            CHECK(textProv->saveAs(target));
            QFile dstFile(dstPath);
            CHECK(dstFile.open(QIODevice::ReadOnly | QIODevice::Text));
            CHECK(QString::fromUtf8(dstFile.readAll()) == QStringLiteral("hello saveAs\n"));
            dstFile.close();
            QFile::remove(srcPath);
            QFile::remove(dstPath);
        }
        // dispose 撤销注册，无悬空。
        if (webDispose)
            webDispose->dispose();
        CHECK(editorSvc->providerFor(QStringLiteral("web")) == nullptr);
    }

    // ---- e2e：内置 settings 编辑器（owner core.settings）----
    {
        // headless 判据：editorViewTypeFor(settings: application/json) 命中 settings2。
        ResourceRef settingsRef{QStringLiteral("settings:"), QStringLiteral("application/json"),
                                QStringLiteral("设置")};
        CHECK(editorSvc->editorViewTypeFor(settingsRef) == QLatin1String("workbench.editor.settings2"));
        // 打开（openEditor 全链：providerFor → canOpen → createEditor）拿到的就是不透明控件。
        QWidget *page = editorSvc->openEditor(QStringLiteral("workbench.editor.settings2"), settingsRef,
                                              application.context(), application.host(), nullptr);
        CHECK(page != nullptr);
        auto *settingsPage = qobject_cast<assembly::SettingsEditorWidget *>(page);
        CHECK(settingsPage != nullptr);
        if (settingsPage)
        {
            using nextpilot::plotscope::workbench::IConfigurationService;
            auto *cfgSvc = services->getService<IConfigurationService>(QStringLiteral("configuration.readback"));
            CHECK(cfgSvc != nullptr);
            if (cfgSvc)
            {
                // 搜索命中一条属性（搜索 key + description）。
                settingsPage->setSearch(QStringLiteral("hello.greeting"));
                CHECK(settingsPage->visibleSettingCount() == 1);
                settingsPage->setSearch({});
                const int allCount = settingsPage->visibleSettingCount();
                CHECK(allCount >= 4); // hello 3 条 + core.settings 通用 1 条
                // 改一个值（走控件写路径 → 服务 → SettingsStore）。
                CHECK(!cfgSvc->inspect(QStringLiteral("hello.greeting")).user.set);
                settingsPage->setSettingValue(QStringLiteral("hello.greeting"), QJsonValue(QStringLiteral("嗨")));
                CHECK(cfgSvc->effectiveValue(QStringLiteral("hello.greeting")).toString() == QStringLiteral("嗨"));
                CHECK(settingsPage->layerHintOf(QStringLiteral("hello.greeting")) == QLatin1String("user"));
                // Modified 过滤：只显示改过的项（hello.greeting 是唯一被改的）。
                settingsPage->setModifiedFilter(true);
                CHECK(settingsPage->visibleSettingCount() == 1);
                settingsPage->setSearch(QStringLiteral("hello.greeting"));
                CHECK(settingsPage->visibleSettingCount() == 1);
                settingsPage->setSearch({});
                settingsPage->setModifiedFilter(false);
                // JSON 视图与属性表一致：改过的值出现在 JSON 中，未改的是默认/合并值。
                settingsPage->setJsonView(true);
                CHECK(settingsPage->jsonText().contains(QStringLiteral("\"hello.greeting\": \"嗨\"")));
                CHECK(settingsPage->jsonText().contains(QStringLiteral("\"hello.count\": 3"))); // 未改 → 默认值
                CHECK(settingsPage->jsonText().contains(
                    QStringLiteral("\"workbench.settings.editor\": \"ui\""))); // core 通用组
                settingsPage->setJsonView(false);
                // 改值落 SettingsStore 后重开仍在：关掉页签重开新页，值还在（服务层读回）。
                settingsPage->setJsonView(true);
                CHECK(settingsPage->jsonText().contains(QStringLiteral("\"hello.greeting\": \"嗨\"")));
                settingsPage->setJsonView(false);
                delete page;
                page = editorSvc->openEditor(QStringLiteral("workbench.editor.settings2"), settingsRef,
                                             application.context(), application.host(), nullptr);
                settingsPage = qobject_cast<assembly::SettingsEditorWidget *>(page);
                CHECK(settingsPage != nullptr);
                if (settingsPage)
                {
                    settingsPage->setSearch(QStringLiteral("hello.greeting"));
                    CHECK(settingsPage->visibleSettingCount() == 1);
                    // 重开的页 JSON 与属性表一致：改过的值仍在（SettingsStore 读回）。
                    settingsPage->setJsonView(true);
                    CHECK(settingsPage->jsonText().contains(QStringLiteral("\"hello.greeting\": \"嗨\"")));
                    settingsPage->setJsonView(false);
                }
                // 逐项 reset：恢复默认，Modified 过滤变空。
                settingsPage->setModifiedFilter(true);
                CHECK(settingsPage->visibleSettingCount() == 1);
                // reset 走行内按钮等价路径（直接调服务 + refresh；行按钮最终也是它）。
                cfgSvc->reset(QStringLiteral("hello.greeting"));
                settingsPage->refresh();
                CHECK(settingsPage->visibleSettingCount() == 0);
                // 零 owner 开设置：工作台自带「通用」组独立可搜索（不依赖任何扩展）。
                settingsPage->setModifiedFilter(false);
                settingsPage->setSearch(QStringLiteral("workbench.settings.editor"));
                CHECK(settingsPage->visibleSettingCount() == 1);
                settingsPage->setSearch({});
            }
        }
        delete page;
        // 改过的值落 SettingsStore（app 唯一出口）。
        // （unmount 残留断言在下方卸载段之后补。）
    }

    // 执行成功：命令面板激活 → executeCommand(id) → intentOp → host.execute → handler。
    Intent intent;
    intent.op = QStringLiteral("hello.say");
    auto result = application.host().execute(intent);
    CHECK(static_cast<bool>(result));
    CHECK(activation.triggeredCount(QStringLiteral("hello.say")) == 1);

    // ---- e2e：workbench.action.openSettings 命令 + 中央区只认 editor ----
    {
        Intent openSettings;
        openSettings.op = QStringLiteral("workbench.action.openSettings");
        auto opened = application.host().execute(openSettings);
        CHECK(static_cast<bool>(opened));
        // 扩展 handler 让出 workbench.* 前缀：触发计数必须归零（不会被扩展 handler 抢）。
        CHECK(activation.triggeredCount(QStringLiteral("workbench.action.openSettings")) == 0);
        // 命令执行后"页签"（编辑器页）能回答自己的 ResourceRef（headless 断言，§3.0.12）。
        auto *wbHandler = services->getService<assembly::WorkbenchCommandHandler>(QStringLiteral("workbench.commands"));
        CHECK(wbHandler != nullptr);
        if (wbHandler)
        {
            auto *page = wbHandler->lastOpenedEditor();
            CHECK(page != nullptr);
            auto *settingsPage = qobject_cast<assembly::SettingsEditorWidget *>(page);
            CHECK(settingsPage != nullptr);
            if (settingsPage)
            {
                const auto ref = settingsPage->resourceRef();
                CHECK(ref.uri == QLatin1String("settings:"));
                CHECK(ref.mime == QLatin1String("application/json"));
                CHECK(ref.title == QStringLiteral("设置"));
            }
        }
    }

    // handler 不认领未贡献的命令（不抢别人的 op）。
    Intent unknown;
    unknown.op = QStringLiteral("no.such.command");
    auto other = application.host().execute(unknown);
    CHECK(!static_cast<bool>(other));

    // 卸载 owner：命令从命令表整批撤，不留悬空。
    // 先造一个用户层残留值，验证 unmount 后组消失、用户数据保留。
    {
        using nextpilot::plotscope::workbench::IConfigurationService;
        auto *cfgSvc = services->getService<IConfigurationService>(QStringLiteral("configuration.readback"));
        CHECK(cfgSvc != nullptr);
        if (cfgSvc)
            cfgSvc->setUserValue(QStringLiteral("hello.verbose"), QJsonValue(true));
    }
    application.context().toolbox().unmountContribution(QStringLiteral("plotscope.helloworld"));
    activation.unbind(QStringLiteral("plotscope.helloworld"), &commands);
    CHECK(!commands.contains(QStringLiteral("hello.say")));
    // core.settings 的内置命令不随扩展卸载（owner 是工作台自带）。
    CHECK(commands.contains(QStringLiteral("workbench.action.openSettings")));
    CHECK(activation.boundCommandCount() == 1);
    // unmount 后配置组消失、键原文残留不生效；诊断只覆盖旧 config/ 命名空间（不删用户数据）。
    {
        using nextpilot::plotscope::workbench::IConfigurationService;
        auto *cfgSvc = services->getService<IConfigurationService>(QStringLiteral("configuration.readback"));
        CHECK(cfgSvc != nullptr);
        if (cfgSvc)
        {
            bool helloGone = true;
            for (const auto &c : cfgSvc->configurations())
                helloGone &= c.title != QStringLiteral("示例设置");
            CHECK(helloGone); // core.settings 通用组仍在，扩展组消失
            const auto ins = cfgSvc->inspect(QStringLiteral("hello.verbose"));
            CHECK(!ins.declared);
            CHECK(ins.effective.type() == QJsonValue::Undefined); // 残留不生效
            // 值写在键原文空间：用户数据保留、但不按前缀误报为残留诊断。
            CHECK(application.context().settings().contains(application.context().settings().scopedKey(
                nextpilot::plotscope::context::SettingsScope::App, QStringLiteral("hello.verbose"))));
            bool keySpaceDiag = false;
            for (const auto &d : cfgSvc->diagnostics())
                keySpaceDiag |= d.key == QLatin1String("hello.verbose");
            CHECK(!keySpaceDiag);
            // 旧 config/ 命名空间里的未声明键仍报"旧 config/"诊断。
            application.context().settings().setScopedValue(nextpilot::plotscope::context::SettingsScope::App,
                                                            QStringLiteral("config/hello.legacy"), 1);
            bool legacyDiag = false;
            for (const auto &d : cfgSvc->diagnostics())
                legacyDiag |=
                    d.key == QLatin1String("hello.legacy") && d.message.contains(QStringLiteral("旧 config/"));
            CHECK(legacyDiag);
            application.context().settings().removeScoped(nextpilot::plotscope::context::SettingsScope::App,
                                                          QStringLiteral("config/hello.legacy"));
        }
    }
    // 视图声明也整批撤回（侧栏容器/视图/标题动作/colors/status 全消失）。
    CHECK(ext.contributedViewContainers().isEmpty());
    CHECK(ext.contributedViews().isEmpty());
    CHECK(ext.menuItemsIn(QStringLiteral("view/title")).isEmpty());
    CHECK(ext.menuItemsIn(QStringLiteral("view/item/context")).isEmpty());
    CHECK(ext.contributedColors().isEmpty());
    CHECK(ext.contributedStatuses().isEmpty());
    if (viewsReg)
    {
        CHECK(viewsReg->provider(QStringLiteral("hello.view")) == nullptr);
    }
    // Step 5：unbind 经同一 ViewRegistry 按 owner 整批撤，服务读回同步为空。
    if (viewsReg)
        CHECK(viewsReg->viewsIn(QStringLiteral("hello.container")).isEmpty());
    if (viewSvc)
    {
        CHECK(viewSvc->viewsInContainer(QStringLiteral("hello.container")).isEmpty());
        CHECK(viewSvc->containersIn(QStringLiteral("sidebar")).isEmpty());
        CHECK(viewSvc->viewForId(QStringLiteral("hello.view")).owner.isEmpty());
    }
    // M3-a/M3-b：unmount 后扩展编辑器声明整批撤，只剩内置 text + settings
    //（core.text / core.settings 不随扩展撤）。
    if (editorSvc)
    {
        CHECK(editorSvc->customEditors().size() == 2);
        bool webGone = true, settingsKept = false;
        for (const auto &ed : editorSvc->customEditors())
        {
            webGone &= ed.viewType != QLatin1String("web");
            settingsKept |= ed.viewType == QLatin1String("workbench.editor.settings2");
        }
        CHECK(webGone);
        CHECK(settingsKept);
        CHECK(editorSvc->editorViewTypeFor(html).isEmpty());
        // 声明没了，再注册 web 必拒（owner 不匹配 → 无声明者）。
        CHECK(viewsReg && viewsReg->registerEditorProvider(QStringLiteral("web"), &g_webProvider,
                                                           QStringLiteral("plotscope.helloworld")) ==
                                      nullptr);
    }
    // host 再 execute 该 op 无 handler 认领。
    auto afterUnmount = application.host().execute(intent);
    CHECK(!static_cast<bool>(afterUnmount));

    if (g_failed == 0)
        std::printf("cmd-ext e2e: ALL PASS\n");
    else
        std::printf("cmd-ext e2e: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
