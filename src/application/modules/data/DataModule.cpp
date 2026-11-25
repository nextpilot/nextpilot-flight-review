#include "DataModule.h"

#include "BuiltinModuleHub.h"
#include "DatasetHub.h"
#include "IngestPipeline.h"
#include "ErrorCodes.h"
#include "Events.h"
#include "Contribution.h"
#include "IWorkbenchContribution.h"
#include "InstantiationService.h"
#include "IWorkspace.h"
#include "ICommands.h"
#include "IScriptCapabilities.h"
#include "RuntimeHost.h"
#include "Result.h"

#include <QList>
#include <QPointer>
#include <QVariantMap>

#include <cstdio>

using nextpilot::plotscope::instantiation::InstantiationService;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
namespace parser = nextpilot::plotscope::parser; // 限定名 parser:: 需要别名（using-directive 不引入命名空间名）
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

ModuleInfo DataModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.data"),
        QStringLiteral("数据模块"),
        QStringLiteral("1.0"),
        {QStringLiteral("data.session")},
        {}
    };
}

IWorkbenchContribution *DataModule::create() { return new DataModule(); }

ModuleInfo DataModule::info() const
{
    return moduleInfo();
}

void DataModule::initialize(InstantiationService &services)
{
    auto *ws = services.getService<nextpilot::plotscope::workbench::IWorkspace>(QStringLiteral("vscode.workspace"));
    auto *cmds = services.getService<nextpilot::plotscope::workbench::ICommands>(QStringLiteral("vscode.commands"));
    auto *caps =
        services.getService<nextpilot::plotscope::uischema::IScriptCapabilities>(QStringLiteral("script.capabilities"));
    if (!ws || !cmds || !caps)
        return;
    // onAttach 的"来源"画廊需要格式驱动元数据；initialize 阶段按名取用并缓存，
    // 避免 onAttach 回头拿宿主（m_host 在 A1b 已不再赋值）。
    m_driverMetas = ws->sourceDriverMetas();
    // 只订阅仓库事件；不碰 UI、不认识格式。§1.5：DatasetHub 信号 → IDatasetAccess Event。
    // PLOTSCOPE_QUIET_STDOUT：MCP stdio 桥等协议场景下 stdout 是协议通道。
    const bool quiet = !qEnvironmentVariableIsEmpty("PLOTSCOPE_QUIET_STDOUT");
    ws->dataset().onDidAddDocument([quiet](const QString &id) {
        if (quiet)
            fprintf(stderr, "[data] dataset added: #%s\n", qPrintable(id));
        else
            fprintf(stdout, "[data] dataset added: #%s\n", qPrintable(id));
    });
    ws->dataset().onDidRemoveDocument([quiet](const QString &id) {
        if (quiet)
            fprintf(stderr, "[data] dataset removed: #%s\n", qPrintable(id));
        else
            fprintf(stdout, "[data] dataset removed: #%s\n", qPrintable(id));
    });
    // 加载生命周期 + 进度 → 状态栏（验收 7）：走 IIngest 事件，模块不认识宿主/AppContext。
    // 事件源头是摄取管道（评论 52436915 裁定挂 IIngest，不抬到 workspace 全局）。
    // QPointer 守护：模块析构后回调自动跳过（adapter 的 connect 归装配层生命周期）。
    QPointer<DataModule> self(this);
    ws->ingest().onDidStartLoad([self]() {
        if (self && self->m_ui)
            self->m_ui->updateStatus(QStringLiteral("data.loading.progress"), QStringLiteral("加载中…"));
    });
    ws->ingest().onDidFinishLoad([self](bool ok) {
        if (self && self->m_ui)
            self->m_ui->updateStatus(QStringLiteral("data.loading.progress"),
                                     ok ? QStringLiteral("空闲") : QStringLiteral("加载失败"));
    });
    ws->ingest().onDidReportProgress([self](int, const QString &text) {
        if (self && self->m_ui)
            self->m_ui->updateStatus(QStringLiteral("data.loading.progress"), text);
    });
    // 数据表格查看器：树右键"查看数据表格" → data.table.open
    // → 按需创建中央页签面板（不与画布争位）→ 转发 data.table.show 绑定数据。
    // 面板实现由视图层自注册（DataTablePanel.cpp 的 PS_REGISTER_PANEL_T），
    // 模块只发字符串 id，不依赖 Qt Widgets，headless 构建保持纯净。
    // 注册在 initialize（host 可用）；m_ui 在 onAttach 才有——
    // 动作是运行时触发的，届时 GUI 已 attach。
    // 视图动作：A1b 保持 view action 注册（IScriptCapabilities 既有接口，§1.2.1 注释：
    // GUI 侧仍经 dispatchViewAction 消费，A2b 才迁 command）。此前误迁成 registerCommand
    // 导致 GUI 层 DataManagerPanel / 测试按 dispatchViewAction 取不到（998 回归）。
    // 模块只经 caps 注册，不碰 AppContext/interactionHub。
    caps->registerViewAction(
        QStringLiteral("data.table.open"), [this, caps](const QVariantMap &params) -> Result<QVariant> {
            if (!m_ui)
                return fail(err::Unsupported, QStringLiteral("数据表格需要 GUI 环境"));
            if (!m_ui->requestPanel(nextpilot::plotscope::registry::Region::Editor,
                                    QStringLiteral("data.table")))
                return fail(err::Unsupported, QStringLiteral("数据表格面板不可用"));
            Result<QVariant> out{QVariant()};
            caps->dispatchViewAction(QStringLiteral("data.table.show"), params, out);
            return out;
        });
}

namespace {
// 唯一实现：接受 {id, displayName} 元数据对，两个公开重载都转发到这里。
QList<UiAction> buildSourceGalleryActions(const QVector<QPair<QString, QString>> &metas)
{
    QList<UiAction> actions;
    int fmtOrder = 100;
    int common = 0;
    for (const auto &meta : metas)
    {
        const QString &id = meta.first;
        const QString &displayName = meta.second;
        UiAction fmt;
        fmt.id = QStringLiteral("data.source.") + id;
        fmt.label = displayName;
        fmt.slotId = QStringLiteral("ribbon.group.data.sources");
        fmt.visual = id;
        fmt.galleryGroup = QStringLiteral("文件格式");
        fmt.icon = QStringLiteral("file_open");
        fmt.intentOp = QStringLiteral("data.load");
        fmt.order = fmtOrder++;
        actions.append(fmt);

        // 前四个驱动同时进"常用"组；排序权重独立于文件格式组。
        if (common < 4)
        {
            UiAction fav;
            fav.id = QStringLiteral("data.source.common.") + id;
            fav.label = displayName;
            fav.slotId = fmt.slotId;
            fav.visual = id;
            fav.galleryGroup = QStringLiteral("常用");
            fav.icon = QStringLiteral("file_open");
            fav.intentOp = QStringLiteral("data.load");
            fav.order = common++;
            actions.append(fav);
        }
    }
    return actions;
}
} // namespace

QList<UiAction> DataModule::sourceGalleryActions(const QList<parser::IMessageParser *> &drivers)
{
    QVector<QPair<QString, QString>> metas;
    metas.reserve(drivers.size());
    for (const parser::IMessageParser *d : drivers)
        if (d)
            metas.append({d->id(), d->displayName()});
    return buildSourceGalleryActions(metas);
}

QList<UiAction> DataModule::sourceGalleryActions(const QVector<QPair<QString, QString>> &metas)
{
    return buildSourceGalleryActions(metas);
}

ModuleContributions DataModule::contributes() const
{
    ModuleContributions c;
    c.owner = QStringLiteral("data");

    {
        ThemeTokenSpec t;
        t.id = QStringLiteral("data.source");
        t.fallback = QStringLiteral("#f2b134");
        t.description = QStringLiteral("数据源标识颜色");
        c.themeTokens.append(t);
    }

    // 状态栏项：数据源数量、加载进度、回放状态。文本由本模块经 updateStatus 推送。
    {
        StatusSpec s;
        s.id = QStringLiteral("data.source.count");
        s.side = StatusSide::Left;
        s.priority = 10;
        s.tooltip = QStringLiteral("已载入数据源数量");
        s.provider = QStringLiteral("data");
        s.text = QStringLiteral("数据源: 0");
        c.statuses.append(s);

        StatusSpec p;
        p.id = QStringLiteral("data.loading.progress");
        p.side = StatusSide::Left;
        p.priority = 20;
        p.tooltip = QStringLiteral("数据加载进度");
        p.provider = QStringLiteral("data");
        p.text = QStringLiteral("空闲");
        c.statuses.append(p);

        StatusSpec play;
        play.id = QStringLiteral("playback.state");
        play.side = StatusSide::Right;
        play.priority = 10;
        play.tooltip = QStringLiteral("回放状态");
        play.provider = QStringLiteral("data");
        play.text = QStringLiteral("停止");
        c.statuses.append(play);
    }

    c.ribbonPages.append(RibbonPageDecl{QStringLiteral("ribbon.page.data"),
                                        QStringLiteral("数据"), 2});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.data.common"),
                                          QStringLiteral("常用"), QStringLiteral("ribbon.page.data"), 0});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.data.record"),
                                          QStringLiteral("录制"), QStringLiteral("ribbon.page.data"), 1});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.data.sources"),
                                          QStringLiteral("来源"), QStringLiteral("ribbon.page.data"), 2});

    // 来源画廊：条目由 initialize 缓存的格式驱动元数据派生，壳不保留格式常量表。
    for (const UiAction &a : sourceGalleryActions(m_driverMetas))
        c.actions.append(a);

    ActivitySpec dataActivity;
    dataActivity.id = QStringLiteral("data");
    dataActivity.label = QStringLiteral("数据");
    dataActivity.icon = QStringLiteral("datasource");
    dataActivity.order = 0;
    dataActivity.ribbonPage = QStringLiteral("ribbon.page.data");
    dataActivity.panelTypes = {QStringLiteral("dataset-tree")};
    c.activities.append(dataActivity);

    // 开始页"常用"组：打开 / 最近。
    auto addHome = [&c](const QString &id, const QString &label, const QString &op,
                        const QString &icon, const QString &shortcut, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = QStringLiteral("ribbon.group.home.data");
        a.intentOp = op;
        a.icon = icon;
        a.shortcut = shortcut;
        a.order = order;
        c.actions.append(a);
    };
    addHome(QStringLiteral("app.action.openData"), QStringLiteral("打开数据"),
            QStringLiteral("data.load"), QStringLiteral("file_open"), QStringLiteral("Ctrl+O"), 0);
    addHome(QStringLiteral("home.action.recent"), QStringLiteral("最近打开"),
            QStringLiteral("data.recent"), QStringLiteral("history"), QString(), 1);

    // 数据功能 ribbon 的"常用"动作（每个绑定真实存在的意图处理器）。
    auto add = [&c](const QString &id, const QString &label,
                    const QString &op, const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = QStringLiteral("ribbon.group.data.common");
        a.intentOp = op;
        a.icon = icon;
        a.order = order;
        c.actions.append(a);
    };
    add(QStringLiteral("data.action.quickOpen"), QStringLiteral("快速打开"),
        QStringLiteral("data.load"), QStringLiteral("open"), 0);
    add(QStringLiteral("data.action.recent"), QStringLiteral("最近"),
        QStringLiteral("data.recent"), QStringLiteral("history"), 1);
    add(QStringLiteral("data.action.reload"), QStringLiteral("重载"),
        QStringLiteral("data.reload"), QStringLiteral("reset"), 10);
    add(QStringLiteral("data.action.export"), QStringLiteral("导出 CSV"),
        QStringLiteral("data.export"), QStringLiteral("export_data"), 11);
    add(QStringLiteral("data.action.clearCache"), QStringLiteral("清解析缓存"),
        QStringLiteral("data.clearCache"), QStringLiteral("trash"), 12);

    // 录制组：两态按钮。
    auto addRec = [&c](const QString &id, const QString &label, const QString &op,
                       const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = QStringLiteral("ribbon.group.data.record");
        a.intentOp = op;
        a.icon = icon;
        a.order = order;
        c.actions.append(a);
    };
    addRec(QStringLiteral("data.action.recordStart"), QStringLiteral("开始录制"),
           QStringLiteral("data.record.start"), QStringLiteral("record"), 20);
    addRec(QStringLiteral("data.action.recordStop"), QStringLiteral("停止录制"),
           QStringLiteral("data.record.stop"), QStringLiteral("pause"), 21);
    return c;
}

void DataModule::onAttach(IUiContext &ui)
{
    // A8b-M2：声明已迁至 contributes()，宿主统一灌回；onAttach 只保留运行时服务。
    m_ui = &ui;

    // 数据源数量：订阅 DatasetHub 增删信号，推模式更新（不轮询）。
    if (m_host)
    {
        DatasetHub &hub = m_host->app().data();
        const auto refreshCount = [this, &hub]() {
            if (m_ui)
                m_ui->updateStatus(QStringLiteral("data.source.count"),
                                   QStringLiteral("数据源: %1").arg(hub.ids().size()));
        };
        QObject::connect(&hub, &DatasetHub::datasetAdded, this,
                         [refreshCount](DatasetId) { refreshCount(); });
        QObject::connect(&hub, &DatasetHub::datasetRemoved, this,
                         [refreshCount](DatasetId) { refreshCount(); });
        refreshCount();

        AppContext &appCtx = m_host->app();
        QObject::connect(&appCtx, &AppContext::dataLoadStarted, this, [this]() {
            if (m_ui)
                m_ui->updateStatus(QStringLiteral("data.loading.progress"),
                                   QStringLiteral("加载中…"));
        });
        QObject::connect(&appCtx, &AppContext::dataLoadFinished, this, [this](bool ok) {
            if (m_ui)
                m_ui->updateStatus(QStringLiteral("data.loading.progress"),
                                   ok ? QStringLiteral("空闲") : QStringLiteral("加载失败"));
        });

        IngestPipeline &ingest = appCtx.ingest();
        QObject::connect(&ingest, &IngestPipeline::progressChanged, this,
                         [this](int, uint64_t done, uint64_t total) {
                             if (!m_ui)
                                 return;
                             const QString text = total > 0
                                 ? QStringLiteral("加载 %1%").arg(done * 100 / total)
                                 : QStringLiteral("加载 %1 字节").arg(done);
                             m_ui->updateStatus(QStringLiteral("data.loading.progress"), text);
                         });
    }

    // 数据集树面板（运行时服务）。
    ui.requestPanel(nextpilot::plotscope::registry::Region::PrimarySidebar,
                    QStringLiteral("dataset-tree"));
}

#include "DataModule.moc"

// 编译期自注册：宿主启动时遍历全局表装载，新增模块无需改 App / 宿主。
PS_REGISTER_MODULE(DataModule)
