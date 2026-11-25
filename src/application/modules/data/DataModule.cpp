#include "DataModule.h"

#include "BuiltinModuleHub.h"
#include "DatasetHub.h"
#include "IngestPipeline.h"
#include "ErrorCodes.h"
#include "Events.h"
#include "Contribution.h"
#include "IModule.h"
#include "RuntimeHost.h"
#include "Result.h"

#include <QList>
#include <QVariantMap>

#include <cstdio>

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

IModule *DataModule::create()
{
    return new DataModule();
}

ModuleInfo DataModule::info() const
{
    return moduleInfo();
}

void DataModule::initialize(RuntimeHost &host)
{
    m_host = &host;
    // 只订阅仓库事件；不碰 UI、不认识格式。用 this 作 context object，
    // 模块析构时连接自动断开——不需要手动解绑。
    // PLOTSCOPE_QUIET_STDOUT：MCP stdio 桥等协议场景下 stdout 是协议通道，
    // 提示打印必须让路（stderr 随便打，stdout 一个字节都不能混入）。
    const bool quiet = !qEnvironmentVariableIsEmpty("PLOTSCOPE_QUIET_STDOUT");
    connect(&host.app().data(), &DatasetHub::datasetAdded, this, [quiet](DatasetId id) {
        if (quiet)
            fprintf(stderr, "[data] dataset added: #%d\n", id);
        else
            fprintf(stdout, "[data] dataset added: #%d\n", id);
    });
    connect(&host.app().data(), &DatasetHub::datasetRemoved, this, [quiet](DatasetId id) {
        if (quiet)
            fprintf(stderr, "[data] dataset removed: #%d\n", id);
        else
            fprintf(stdout, "[data] dataset removed: #%d\n", id);
    });
    // 数据表格查看器：树右键"查看数据表格" → data.table.open
    // → 按需创建中央页签面板（不与画布争位）→ 转发 data.table.show 绑定数据。
    // 面板实现由视图层自注册（DataTablePanel.cpp 的 PS_REGISTER_PANEL_T），
    // 模块只发字符串 id，不依赖 Qt Widgets，headless 构建保持纯净。
    // 注册在 initialize（host 可用）；m_ui 在 onAttach 才有——
    // 动作是运行时触发的，届时 GUI 已 attach。
    AppContext &appCtx = host.app();

    // A0b 清场：活动通道取数接缝注册（Q1 违规领域接缝）已按施工令删除，
    // A1b 将以泛化能力接缝重接。
    appCtx.interactions().registerViewAction(
        QStringLiteral("data.table.open"),
        [this, &appCtx](const QVariantMap &params) -> Result<QVariant> {
            if (!m_ui)
                return fail(err::Unsupported, QStringLiteral("数据表格需要 GUI 环境"));
            if (!m_ui->requestPanel(nextpilot::plotscope::registry::Region::Editor,
                                    QStringLiteral("data.table")))
                return fail(err::Unsupported, QStringLiteral("数据表格面板不可用"));
            Result<QVariant> out{QVariant()};
            appCtx.interactions().dispatchViewAction(
                QStringLiteral("data.table.show"), params, out);
            return out;
        });
}

QList<UiAction> DataModule::sourceGalleryActions(const QList<parser::IMessageParser *> &drivers)
{
    QList<UiAction> actions;
    int fmtOrder = 100;
    int common = 0;
    for (parser::IMessageParser *driver : drivers)
    {
        if (!driver)
            continue;
        UiAction fmt;
        fmt.id = QStringLiteral("data.source.") + driver->id();
        fmt.label = driver->displayName();
        fmt.slotId = QStringLiteral("ribbon.group.data.sources");
        fmt.visual = driver->id();
        fmt.galleryGroup = QStringLiteral("文件格式");
        fmt.icon = QStringLiteral("file_open");
        fmt.intentOp = QStringLiteral("data.load");
        fmt.order = fmtOrder++;
        actions.append(fmt);

        // 前四个驱动同时进"常用"组；排序权重独立于文件格式组。
        if (common < 4)
        {
            UiAction fav;
            fav.id = QStringLiteral("data.source.common.") + driver->id();
            fav.label = driver->displayName();
            fav.slotId = fmt.slotId;
            fav.visual = driver->id();
            fav.galleryGroup = QStringLiteral("常用");
            fav.icon = QStringLiteral("file_open");
            fav.intentOp = QStringLiteral("data.load");
            fav.order = common++;
            actions.append(fav);
        }
    }
    return actions;
}

void DataModule::onAttach(IUiContext &ui)
{
    // 语义色 token（验收 8）：数据源标识色由模块声明，壳零硬编码。
    {
        ThemeTokenSpec t;
        t.id = QStringLiteral("data.source");
        // fallback 与 SourceRowDelegate 激活竖条的历史色一致：接管 token 不改变外观。
        t.fallback = QStringLiteral("#f2b134");
        t.description = QStringLiteral("数据源标识颜色");
        ui.declareThemeToken(t);
    }

    m_ui = &ui;

    // 状态栏项（验收 7 / D8）：数据源数量、加载进度、回放状态。
    // 壳按 side/priority 排布；文本由本模块在数据变化时经 updateStatus 推送。
    {
        StatusSpec s;
        s.id = QStringLiteral("data.source.count");
        s.side = StatusSide::Left;
        s.priority = 10;
        s.tooltip = QStringLiteral("已载入数据源数量");
        s.provider = QStringLiteral("data");
        s.text = QStringLiteral("数据源: 0");
        ui.declareStatus(s);

        StatusSpec p;
        p.id = QStringLiteral("data.loading.progress");
        p.side = StatusSide::Left;
        p.priority = 20;
        p.tooltip = QStringLiteral("数据加载进度");
        p.provider = QStringLiteral("data");
        p.text = QStringLiteral("空闲");
        ui.declareStatus(p);

        StatusSpec play;
        play.id = QStringLiteral("playback.state");
        play.side = StatusSide::Right;
        play.priority = 10;
        play.tooltip = QStringLiteral("回放状态");
        play.provider = QStringLiteral("data");
        play.text = QStringLiteral("停止");
        ui.declareStatus(play);
    }
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

        // 加载进度：订阅上下文加载生命周期（真实 data.load 摄取入口驱动，非定时器刷值）。
        // 开始摄取 → "加载中…"；摄取返回 → 成功回"空闲"，失败置"加载失败"。
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

        // 加载进度（验收 7）：订阅摄取管道的真实进度，覆盖上面 start 端的"加载中…"占位。
        // total=0（CSV 等流式 parser 上报 consumed 字节、总量未知）显示"已读 N 字节"；
        // total>0 显示百分比。finish 端仍回"空闲"/"加载失败"，不由本订阅收尾。
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
    // GUI 环境下才走到这里（headless 时宿主不调用 onAttach）。
    //
    // 模块只声明"我需要一个数据集树面板"，由 app 骨架的部件工厂决定用哪个实现——
    // 因此本模块不依赖 Qt Widgets，CLI（headless）构建保持纯净。

    // 本域 Ribbon 页/组自声明（D13）：壳不再预置业务页。
    // 注：开始页 ribbon.page.home 是产品级聚合页，暂由壳 ShellContributions 声明；
    // 本模块只把"打开/最近"作为业务条目挂进 home.data 组，不声明该页/组容器。
    ui.declareRibbonPage(QStringLiteral("ribbon.page.data"), QStringLiteral("数据"), 2);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.data.common"), QStringLiteral("常用"),
                          QStringLiteral("ribbon.page.data"), 0);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.data.record"), QStringLiteral("录制"),
                          QStringLiteral("ribbon.page.data"), 1);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.data.sources"), QStringLiteral("来源"),
                          QStringLiteral("ribbon.page.data"), 2);
    // 注：原 ribbon.group.data.debug「调试」组在 c7 解散 SkeletonSlots 时声明，
    // 但从未有任何动作绑定（D13 零消费者死槽），验收 15 的零消费者检查抓到后删除。

    // 来源画廊：条目由已装载的格式驱动元数据派生（sourceGalleryActions），
    // 壳不保留格式常量表；装/卸解析器，图块随之增减。
    const QList<parser::IMessageParser *> drivers =
        m_host ? m_host->app().toolbox().drivers() : QList<parser::IMessageParser *>{};
    for (const UiAction &a : sourceGalleryActions(drivers))
        ui.addAction(a);

    ActivitySpec dataActivity;
    dataActivity.id = QStringLiteral("data");
    dataActivity.label = QStringLiteral("数据");
    dataActivity.icon = QStringLiteral("datasource");
    dataActivity.order = 0;
    dataActivity.ribbonPage = QStringLiteral("ribbon.page.data");
    dataActivity.panelTypes = {QStringLiteral("dataset-tree")};
    ui.declareActivity(dataActivity);
    ui.requestPanel(nextpilot::plotscope::registry::Region::PrimarySidebar,
                    QStringLiteral("dataset-tree"));

    // 开始页"常用"组：打开 / 最近（落地页高频入口，id 与外壳文件对话框特判对齐）。
    // 组容器由壳 ShellContributions 声明，这里只贡献业务条目。
    auto addHome = [&ui](const QString &id, const QString &label, const QString &op,
                         const QString &icon, const QString &shortcut, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = QStringLiteral("ribbon.group.home.data");
        a.intentOp = op;
        a.icon = icon;
        a.shortcut = shortcut;
        a.order = order;
        ui.addAction(a);
    };
    addHome(QStringLiteral("app.action.openData"), QStringLiteral("打开数据"),
            QStringLiteral("data.load"), QStringLiteral("file_open"), QStringLiteral("Ctrl+O"), 0);
    addHome(QStringLiteral("home.action.recent"), QStringLiteral("最近打开"),
            QStringLiteral("data.recent"), QStringLiteral("history"), QString(), 1);

    // 数据功能 ribbon 的"常用"动作。
    // 每个都绑定**真实存在**的意图处理器（宿主级 data.* handler），不是空按钮。
    // 快速打开/最近占 0/1，重载/导出/清缓存从 10 起排。
    auto add = [&ui](const QString &id, const QString &label,
                     const QString &op, const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = QStringLiteral("ribbon.group.data.common");
        a.intentOp = op;
        a.icon = icon;
        a.order = order;
        ui.addAction(a);
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

    // 录制组：两态按钮，参数全走缺省——活动源 + raw/file + 自动文件名，
    // 点一下就录，不弹对话框（要选格式/后端属于高级用法，可走 CLI/脚本传参）。
    auto addRec = [&ui](const QString &id, const QString &label, const QString &op,
                        const QString &icon, int order) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.slotId = QStringLiteral("ribbon.group.data.record");
        a.intentOp = op;
        a.icon = icon;
        a.order = order;
        ui.addAction(a);
    };
    addRec(QStringLiteral("data.action.recordStart"), QStringLiteral("开始录制"),
           QStringLiteral("data.record.start"), QStringLiteral("record"), 20);
    addRec(QStringLiteral("data.action.recordStop"), QStringLiteral("停止录制"),
           QStringLiteral("data.record.stop"), QStringLiteral("pause"), 21);
}

#include "DataModule.moc"

// 编译期自注册：宿主启动时遍历全局表装载，新增模块无需改 App / 宿主。
PS_REGISTER_MODULE(DataModule)
