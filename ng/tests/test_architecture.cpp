#include "App.h"
#include "Intent.h"
#include "WorkspaceDataConfig.h"
#include "DataRepository.h"
#include "Dataset.h"
#include "ExtensionRegistry.h"
#include "UiSlotRegistry.h"
#include "PluginScanner.h"
#include "Toolbox.h"
#include "BuiltinContributions.h"
#include "BuiltinModuleRegistry.h"
#include "BuiltinPackRegistry.h"
#include "IModule.h"
#include "IPanelContribution.h"
#include "DecodedFrame.h"
#include "IMessageParser.h"
#include "NmeaMessageParser.h"

#include <QCoreApplication>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

// Release(NDEBUG) 下 assert 是空操作；用 CHECK 做真实断言，避免有副作用的
// 调用被 assert 包裹后在 Release 里整个消失（历史踩过坑）。
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

// ============================================================================
// 「新增一个 module / extension 不修改其他代码」的证明
//
// 下面这两个假模块与假包**只在本测试文件里**定义并静态自注册。
// 它们的加入没有改动宿主、App、注册表或任何既有模块/扩展的一行代码——
// 这正是自注册机制要兑现的承诺：如果哪天必须去改 App.cpp 才能加一个模块，
// 这条测试就会失败，提醒我们机制退化了。
// ============================================================================

namespace
{

// 探针模块的意图处理器。普通类即可（不需要 Q_OBJECT：它不发信号）。
class ProbeHandler final : public IIntentHandler
{
public:
    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("probe.ping");
    }
    Result<IntentResult> execute(const Intent &) override
    {
        IntentResult r;
        r.payload = QStringLiteral("pong");
        return r;
    }
};

// 假模块：声明一个能力，提供一个意图处理器。
class ProbeModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    static nextpilot::plotscope::host::ModuleInfo moduleInfo()
    {
        return nextpilot::plotscope::host::ModuleInfo{QStringLiteral("mod.probe"),
                                    QStringLiteral("探针模块"),
                                    QStringLiteral("1.0"),
                                    nextpilot::plotscope::host::ModuleKind::Builtin,
                                    {QStringLiteral("probe.capability")},
                                    {}};
    }
    static nextpilot::plotscope::host::IModule *create() { return new ProbeModule(); }

    nextpilot::plotscope::host::ModuleInfo info() const override { return moduleInfo(); }

    void initialize(nextpilot::plotscope::host::ModuleContext &ctx) override
    {
        ctx.registerIntentHandler(new ProbeHandler);
    }
};

// 假知识包：带一个 UI 贡献。
// 用工厂函数而非继承（ToolPack 是 final 类）：静态自注册机制只要求
// 提供 packId() 与 build() 两个静态接口。
struct ProbePack
{
    static QString packId() { return QStringLiteral("pack.probe"); }
    static std::unique_ptr<IToolPack> build()
    {
        auto pack = std::make_unique<ToolPack>(packId(), QStringLiteral("探针工具箱"),
                                               Domain::General);
        pack->add<nextpilot::plotscope::ext::GenericUiContribution>(
            QStringLiteral("probe.ui"), QStringLiteral("探针界面入口"), Domain::General,
            QList<UiAction>{{QStringLiteral("probe.ui.act"), QStringLiteral("探针动作"),
                             QStringLiteral("menu.plot"), QString(), QString()}});
        return pack;
    }
};

} // namespace

// 静态自注册：与生产代码里的模块/包完全同一套机制。
NG_REGISTER_MODULE(::ProbeModule)
NG_REGISTER_PACK(::ProbePack)

static QString writeCsv(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("demo.csv"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&f);
    out << "time,roll,pitch\n"
        << "0,1.0,2.0\n"
        << "1,1.5,2.5\n"
        << "2,2.0,3.0\n";
    f.close();
    return path;
}

// ---- 合成 ULog v1 测试样本 ------------------------------------------------
// 复用旧 extension 的协议知识手工构造：一个 F 格式定义 + A 订阅 + N 条 D 数据，
// 外加 L 事件段。timestamp 为 U64 微秒，字段 float x / uint16 y（验证 typed 列）。
static void put16(QByteArray &b, quint16 v)
{
    b.append(static_cast<char>(v & 0xff));
    b.append(static_cast<char>((v >> 8) & 0xff));
}
static void put32(QByteArray &b, quint32 v)
{
    for (int i = 0; i < 4; ++i) b.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void put64(QByteArray &b, quint64 v)
{
    for (int i = 0; i < 8; ++i) b.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void putFloat(QByteArray &b, float v)
{
    quint32 bits = 0;
    std::memcpy(&bits, &v, 4);
    put32(b, bits);
}

static void addUlogMessage(QByteArray &out, char type, const QByteArray &body)
{
    put16(out, static_cast<quint16>(body.size()));
    out.append(type);
    out.append(body);
}

// 生成 rows 行、每 topic 两列（F32 x / U16 y）的 ULog；覆盖 kChunkRows=4096 的分批。
static QString writeUlog(const QTemporaryDir &dir, int rows, qint64 *firstUs, qint64 *lastUs)
{
    const QByteArray f("sensor:uint64_t timestamp;float x;uint16_t y;");
    const QString path = dir.filePath(QStringLiteral("demo.ulg"));
    QByteArray out;
    // 头：magic(7) + version(1) + start_timestamp(8)
    out.append("ULog", 4);
    out.append(static_cast<char>(0x01));
    out.append(static_cast<char>(0x12));
    out.append(static_cast<char>(0x35));
    out.append(static_cast<char>(0x01)); // version v1
    put64(out, 0);                        // start timestamp

    addUlogMessage(out, 'F', f);
    QByteArray a;
    a.append(static_cast<char>(0));  // multiId
    put16(a, 1);                     // msgId
    a.append("sensor", 6);
    a.append(static_cast<char>(0));  // cstring terminator
    addUlogMessage(out, 'A', a);

    *firstUs = 1000000; // 起始 1.0s
    for (int i = 0; i < rows; ++i)
    {
        const quint64 ts = static_cast<quint64>(*firstUs) + static_cast<quint64>(i) * 1000ULL; // 每行 1ms
        QByteArray d;
        put16(d, 1);          // msgId
        put64(d, ts);         // timestamp U64 微秒
        putFloat(d, static_cast<float>(i) * 0.5f); // x = i*0.5
        put16(d, static_cast<quint16>(i % 1000));   // y
        addUlogMessage(out, 'D', d);
        *lastUs = static_cast<qint64>(ts);
    }
    // L 事件段：level=6, timestamp=1.5s, "armed"
    QByteArray l;
    l.append(static_cast<char>(6));
    put64(l, 1500000);
    l.append("armed", 5);
    addUlogMessage(out, 'L', l);

    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(out);
    file.close();
    return path;
}

static Intent loadIntent(const QString &path)
{
    Intent i;
    i.op = QStringLiteral("data.load");
    i.params[QStringLiteral("path")] = path;
    i.params[QStringLiteral("format")] = QStringLiteral("csv");
    return i;
}

static Intent renderIntent(DatasetId id, const QString &channel)
{
    Intent i;
    i.op = QStringLiteral("plot.render");
    i.params[QStringLiteral("dataset")] = id;
    if (!channel.isEmpty())
        i.params[QStringLiteral("channel")] = channel;
    return i;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString csv = writeCsv(dir);

    // 1) 器官与工具箱分离装载：
    //    - 器官模块含内置 3 个（data/plot/script）+ 本文件自注册的探针模块；
    //    - 格式不再是器官，而是工具箱里的驱动；
    //    - 外部插件扫描入口存在（本轮无插件，报告为空但接口可用）。
    //
    // 注意"探针"这两项：它们只在本测试文件里定义并自注册，宿主/App/注册表
    // 以及所有既有模块与扩展的代码一行未改。它们能被装载，正是
    // 「新增 module / extension 不修改其他代码」的直接证明。
    App application;
    const auto errors = application.startup(true);
    CHECK(errors.isEmpty());
    CHECK(application.host().activeIds().size() == 7); // 6 内置 + 1 探针
    CHECK(application.host().isActive(QStringLiteral("mod.probe")));
    // 内置器官：数据/绘图/脚本/设置 + 本轮补齐的协议/分析。
    CHECK(application.host().isActive(QStringLiteral("mod.settings")));
    CHECK(application.host().isActive(QStringLiteral("mod.protocol")));
    CHECK(application.host().isActive(QStringLiteral("mod.analysis")));

    // 探针模块提供的能力可用（它自带的意图处理器已被注册）。
    {
        Intent ping;
        ping.op = QStringLiteral("probe.ping");
        const auto pong = application.host().execute(ping);
        CHECK(pong);
        CHECK(pong.value().payload.toString() == QStringLiteral("pong"));
    }
    fprintf(stdout, "[ok] 零改动新增：测试内自注册的探针模块被自动装载并可执行\n");

    // 协议模块的意图面已真正接进宿主分发——不是"模块里有个能跑的静态类"。
    // 走 host.execute 才证明脚本/CLI 拿到的就是这份实现。
    {
        Intent v;
        v.op = QStringLiteral("protocol.validate");
        v.params.insert(QStringLiteral("template"),
                        QStringLiteral(R"({"name":"arch_tpl","mode":"bin","layout":"sequential",
                            "segments":[{"name":"a","type":"u8"}]})"));
        const auto r = application.host().execute(v);
        CHECK(r.ok());
        CHECK(r.value().payload.toMap().value(QStringLiteral("ok")).toBool());

        // 非法模板经同一路径必须带回结构化原因，而不是空错误。
        Intent bad = v;
        bad.params.insert(QStringLiteral("template"),
                          QStringLiteral(R"({"name":"x","segments":[{"name":"a","type":"u8"}]})"));
        const auto rb = application.host().execute(bad);
        CHECK(rb.ok()); // 意图本身成功执行
        CHECK(!rb.value().payload.toMap().value(QStringLiteral("ok")).toBool());
        CHECK(!rb.value().payload.toMap().value(QStringLiteral("issues")).toStringList().isEmpty());
    }
    fprintf(stdout, "[ok] 协议模块意图接入宿主分发：protocol.validate 经 host.execute 可执行\n");

    ExtensionRegistry &toolbox = application.host().extensions();
    // 断言语义（关键格式已装入）而非硬编码总数——加一个格式驱动不该让契约测试变红。
    CHECK(toolbox.count() >= 3);
    CHECK(toolbox.contains(QStringLiteral("csv")));
    CHECK(toolbox.contains(QStringLiteral("ulog")));
    CHECK(toolbox.contains(QStringLiteral("custom-protocol"))); // 自定义协议格式
    // 探针包也自动进了工具箱（同样零改动）。
    CHECK(toolbox.packIds().contains(QStringLiteral("pack.probe")));
    fprintf(stdout, "[ok] 零改动新增：测试内自注册的探针知识包被自动装入工具箱\n");
    // 两种发现路径：按 format 精确查找 + 按文件后缀推断。
    CHECK(toolbox.driverForFormat(QStringLiteral("csv")) != nullptr);
    CHECK(toolbox.driverForFormat(QStringLiteral("ulog")) != nullptr);
    CHECK(toolbox.driverForPath(QStringLiteral("x.ULG"))->id() == QStringLiteral("ulog"));
    CHECK(toolbox.driverForPath(QStringLiteral("x.csv"))->id() == QStringLiteral("csv"));

    // 工具箱按业务领域分类：CSV=通用，ULog=飞控。
    CHECK(toolbox.driverForFormat(QStringLiteral("csv"))->domain() == Domain::General);
    CHECK(toolbox.driverForFormat(QStringLiteral("ulog"))->domain() == Domain::Autopilot);
    CHECK(toolbox.driverForFormat(QStringLiteral("custom-protocol"))->domain() == Domain::General);
    CHECK(toolbox.driversByDomain(Domain::General).size() >= 2); // CSV + 自定义协议
    CHECK(toolbox.driversByDomain(Domain::Autopilot).size() == 1);
    CHECK(toolbox.driversByDomain(Domain::Automotive).isEmpty()); // 预留领域暂无驱动
    CHECK(toolbox.domains().size() == 2);
    CHECK(QString::fromLatin1(domainKey(Domain::Gnss)) == QStringLiteral("gnss"));
    CHECK(domainName(Domain::Robotics) == QStringLiteral("机器人"));

    // 外部插件扫描（本轮预留入口）：空目录返回空报告，不报错、不影响内置格式。
    const int formatsBeforeScan = toolbox.count();
    nextpilot::plotscope::ext::PluginScanReport scan = nextpilot::plotscope::ext::scanExternalPlugins(toolbox, dir.path());
    CHECK(scan.discovered.isEmpty());
    CHECK(toolbox.count() == formatsBeforeScan); // 扫描未改动工具箱
    fprintf(stdout, "[ok] 器官(3) 与工具箱(%d 格式驱动) 分离装载；自注册 + 后缀推断 + 插件扫描入口就绪\n",
            formatsBeforeScan);

    // 1b) 工具箱是开放贡献点系统：知识包装载/卸载 + 六类贡献 + (kind,domain) 二维索引。
    {
        // 五个业务方向各一个知识包 + 本文件自注册的探针包；
        // 包是装载/卸载的原子单位。
        CHECK(toolbox.packCount() == 6);
        CHECK(toolbox.pack(QStringLiteral("pack.common")) != nullptr);
        CHECK(toolbox.pack(QStringLiteral("pack.autopilot")) != nullptr);
        CHECK(toolbox.pack(QStringLiteral("pack.automotive")) != nullptr);
        CHECK(toolbox.pack(QStringLiteral("pack.robotics")) != nullptr);
        CHECK(toolbox.pack(QStringLiteral("pack.gnss")) != nullptr);
        // 飞控包装载了全部六类贡献——一个包能同时贡献格式/UI/脚本/数据/例子/文档。
        nextpilot::plotscope::registry::IToolPack *ap = toolbox.pack(QStringLiteral("pack.autopilot"));
        CHECK(ap->contributions().size() == 6);
        QSet<QString> apKinds;
        for (const auto &c : ap->contributions())
            apKinds.insert(c->kind());
        CHECK(apKinds.contains(ContributionKind::Message));
        CHECK(apKinds.contains(ContributionKind::Ui));
        CHECK(apKinds.contains(ContributionKind::Script));
        CHECK(apKinds.contains(ContributionKind::Data));
        CHECK(apKinds.contains(ContributionKind::Example));
        CHECK(apKinds.contains(ContributionKind::Doc));
        // 每个贡献的 provider 指向所属包（按包原子装卸的依据）。
        for (const auto &c : ap->contributions())
            CHECK(c->provider() == QStringLiteral("pack.autopilot"));

        // 七类贡献都能按 kind 查到；开放 kind 用字符串而非枚举。
        // （计数含本测试文件自注册的探针包贡献：+1 个 UI）
        CHECK(toolbox.drivers().size() == 3); // csv + ulog + custom-protocol（统一为 message kind，FrameBatch 输出）
        CHECK(toolbox.contributions(ContributionKind::Ui).size() == 4);
        CHECK(toolbox.contributions(ContributionKind::Panel).size() == 1);
        CHECK(toolbox.contributions(ContributionKind::Script).size() == 2);
        CHECK(toolbox.contributions(ContributionKind::Data).size() == 1);
        CHECK(toolbox.contributions(ContributionKind::Example).size() == 1);
        CHECK(toolbox.contributions(ContributionKind::Doc).size() == 5);
        // 统一后所有解析器（3 格式 + NMEA）都是 message kind，按 outputs() 区分能力。
        CHECK(toolbox.contributions(ContributionKind::Message).size() == 4);
        CHECK(toolbox.parsers().size() == 4);
        CHECK(toolbox.kinds().size() == 7);

        // 二维索引：按 (kind, domain) 精确过滤。
        CHECK(toolbox.contributions(ContributionKind::Ui, Domain::Autopilot).size() == 1);
        CHECK(toolbox.contributions(ContributionKind::Doc, Domain::General).size() == 1);
        CHECK(toolbox.contributionsByDomain(Domain::Autopilot).size() == 6);
        CHECK(toolbox.contributionsByDomain(Domain::Video).isEmpty()); // 预留领域无贡献

        // typed 接口可向下转换拿到领域数据（UI 贡献是纯数据，headless 也成立）。
        nextpilot::plotscope::registry::IToolContribution *uiRaw =
            toolbox.contribution(QStringLiteral("ap.ui.contributions"));
        CHECK(uiRaw != nullptr);
        auto *ui = dynamic_cast<nextpilot::plotscope::registry::IUiContribution *>(uiRaw);
        CHECK(ui != nullptr && ui->actions().size() == 3);
        nextpilot::plotscope::registry::IToolContribution *docRaw =
            toolbox.contribution(QStringLiteral("ap.docs"));
        auto *doc = dynamic_cast<nextpilot::plotscope::registry::IDocContribution *>(docRaw);
        CHECK(doc != nullptr && doc->entries().size() == 1);

        // 领域面板的**描述**是纯数据，headless（CLI）也能查到——
        // 实现（QWidget 工厂）只在 GUI 侧注册，两者用 id 关联。
        nextpilot::plotscope::registry::IToolContribution *panelRaw =
            toolbox.contribution(QStringLiteral("car.panels"));
        CHECK(panelRaw != nullptr);
        auto *panel = dynamic_cast<nextpilot::plotscope::registry::IPanelContribution *>(panelRaw);
        CHECK(panel != nullptr && panel->panels().size() == 1);
        CHECK(panel->panels().first().id == QStringLiteral("car.can-browser"));
        CHECK(panel->domain() == Domain::Automotive);

        // UI 插槽：骨架声明插槽，工具箱 UI 贡献按 area 填入；headless 下仍是纯数据。
        // （动作数含本测试文件自注册的探针包贡献：menu.plot +1）
        //
        // 槽数是 11：骨架槽**唯一声明**（host 层 SkeletonSlots），
        // headless 与 GUI 看到同一份（声明点已从 core 挪到 host，core 只留机制）。
        // 其中"图型"(ribbon.group.home.plottype) 与"来源"(ribbon.group.home.sources)
        // 两个分组承载 gallery（plot 类型 / parse 类型），纯数据声明，headless 也可见。
        // 此前这里是 5 —— 因为骨架槽被声明在两处，
        // 幂等的 declareSlot 让 host 层那份信息更少的版本**静默丢弃**了 GUI 那份，
        // 于是 headless 只看到 5 个残缺槽（详见下方 1b 段的契约测试）。
        const auto &slotReg = application.host().uiSlots();
        // Ribbon 8 页结构（开始/文件/数据/绘图/脚本/协议/分析/视图）。
        CHECK(slotReg.slotsOfKind(SlotKind::RibbonPage).size() == 8);
        for (const char *p : {"ribbon.page.home", "ribbon.page.file", "ribbon.page.data",
                              "ribbon.page.plot", "ribbon.page.script", "ribbon.page.protocol",
                              "ribbon.page.analysis", "ribbon.page.view"})
            CHECK(slotReg.slot(QString::fromLatin1(p)) != nullptr);
        // 图型库归绘图页、来源库归数据页。
        CHECK(slotReg.slot(QStringLiteral("ribbon.group.plot.type")) != nullptr);
        CHECK(slotReg.slot(QStringLiteral("ribbon.group.data.sources")) != nullptr);
        CHECK(slotReg.actionsIn(QStringLiteral("menu.plot")).size() == 3); // 姿态曲线 + TF 树 + 探针
        CHECK(slotReg.actionsIn(QStringLiteral("dock.left")).size() == 1); // 调参面板
        CHECK(slotReg.boundActionCount() >= 6);
        CHECK(slotReg.unboundActions().isEmpty()); // 所有贡献都有归属插槽

        // 开放 kind：自定义 kind 无需改注册表/宿主即可登记（证明可扩展，非封闭枚举）。
        auto customPack = std::make_unique<ToolPack>(QStringLiteral("pack.custom"),
                                                     QStringLiteral("自定义工具箱"), Domain::General);
        customPack->add<nextpilot::plotscope::ext::GenericDocContribution>(
            QStringLiteral("custom.thing"), QStringLiteral("自定义能力"), Domain::General,
            QList<nextpilot::plotscope::registry::DocEntry>{});
        CHECK(toolbox.registerPack(std::move(customPack)));
        CHECK(toolbox.contributions(QStringLiteral("doc")).size() == 6); // 新包贡献被按 kind 索引
        CHECK(toolbox.unregisterPack(QStringLiteral("pack.custom")));    // 按包原子撤销
        CHECK(toolbox.contributions(QStringLiteral("doc")).size() == 5);
        fprintf(stdout, "[ok] 开放贡献点系统：5 知识包 / 7 类贡献 / (kind,domain) 二维索引 / UI 插槽 / 按包原子装卸\n");
    }

    // 1b) UI 插槽：骨架槽**唯一声明**，且信息完整。
    //
    // 这条契约来自一次真实缺陷：骨架槽曾在两处声明
    // （host 层 RuntimeHost 构造函数 + GUI 层 MainWindow::declareStandardSlots），
    // 而 declareSlot 是幂等的（先到者为准），于是 host 层那份信息更少的声明
    // （没有 kind/父槽、label 也不准）把 GUI 层那份**静默丢弃**了：
    //   · dock.left 的 label 成了"左侧面板"而非"数据集"；
    //   · ribbon.home / canvas.tool 落成默认 kind 的幽灵槽；
    //   · headless（CLI --list-slots）只看到一半的骨架槽。
    //
    // 现在骨架槽只在 host 层 SkeletonSlots 一处声明（core 只留机制），
    // GUI 与 headless 拿到同一份。这里钉死它的完整性。
    {
        const auto &slotReg = application.host().uiSlots();

        // 骨架槽齐备（headless 也能看到全部 —— 这正是"单一来源"的价值）。
        for (const char *area : {"ribbon.page.home", "ribbon.group.home.data",
                                 "ribbon.group.home.view", "ribbon.group.home.appearance",
                                 "dock.left", "dock.right", "canvas.center", "canvas.tool",
                                 "menu.plot"})
        {
            CHECK(slotReg.slot(QString::fromLatin1(area)) != nullptr);
        }

        // 信息完整：kind 与父槽不能退化成默认值。
        const UiSlot *page = slotReg.slot(QStringLiteral("ribbon.page.home"));
        CHECK(page && page->kind == SlotKind::RibbonPage);
        const UiSlot *grpData = slotReg.slot(QStringLiteral("ribbon.group.home.data"));
        CHECK(grpData && grpData->kind == SlotKind::RibbonGroup);
        CHECK(grpData && grpData->parentArea == QStringLiteral("ribbon.page.home"));
        const UiSlot *canvas = slotReg.slot(QStringLiteral("canvas.center"));
        CHECK(canvas && canvas->kind == SlotKind::Canvas);
        const UiSlot *tool = slotReg.slot(QStringLiteral("canvas.tool"));
        CHECK(tool && tool->kind == SlotKind::Canvas); // 画布工具归画布，不是默认 DockPanel

        // label 要是**准确**的（曾经被 host 层那份"左侧面板"顶掉）。
        const UiSlot *left = slotReg.slot(QStringLiteral("dock.left"));
        CHECK(left && left->label == QStringLiteral("数据集"));
        const UiSlot *right = slotReg.slot(QStringLiteral("dock.right"));
        CHECK(right && right->label == QStringLiteral("属性"));

        // 扩展的每个 UI 贡献都要能找到归属槽（曾有"航迹工具"落进无实现的空槽）。
        CHECK(slotReg.unboundActions().isEmpty());

        fprintf(stdout, "[ok] UI 插槽单一来源：骨架槽唯一声明、kind/父槽/label 完整、无未归属贡献\n");
    }

    // 2) 意图执行：CSV 扩展产出 Dataset（一张 CSV = 一张表）
    Result<IntentResult> loaded = application.host().execute(loadIntent(csv));
    assert(loaded);
    const DatasetId id = loaded.value().payload.toInt();
    const Dataset *ds = application.data().get(id);
    assert(ds != nullptr);

    // 2a-0) 归并的**行为锁**：CSV 必须走统一摄取管道，
    //       于是数据集名是管道生成的 "src<源id>_<解析器id>"，
    //       而不是旧路径里驱动自己起的名字（文件名）。
    //       这条断言的意义：将来谁把 CSV 退回"驱动自己读文件"，测试立刻红。
    assert(ds->name().startsWith(QStringLiteral("src")));
    assert(ds->name().endsWith(QStringLiteral("_csv")));

    // 2a) 多表模型：一张表，表内两列 typed 列，共享 3 行时间轴
    assert(ds->tableCount() == 1);
    const Table *t = ds->table(0);
    assert(t->rowCount() == 3);
    assert(t->columns.size() == 2);
    assert(t->column(QStringLiteral("roll")) != nullptr);
    assert(t->column(QStringLiteral("pitch")) != nullptr);
    assert(t->column(QStringLiteral("roll"))->type == ValueType::F64);
    // 时间轴本体在 Rust；C++ 只存 O(1) 元数据（首/末时间），不复制整条数组。
    assert(t->firstTimeUs == 0);
    assert(t->lastTimeUs == 2000000);
    fprintf(stdout, "[ok] Dataset = 多张二维表；C++ 仅存首尾时间元数据（时间轴本体在 Rust）\n");

    // 2b) 通道投影：table.column -> (秒,值) f64 序列
    const auto ch = ds->channel(QStringLiteral("demo.roll"));
    assert(ch.has_value());
    assert(ch->size() == 3);
    assert(ch->time.first() == 0.0 && ch->time.last() == 2.0);
    assert(ch->value.at(1) == 1.5);
    fprintf(stdout, "[ok] 通道投影 table.column 产出绘图用 (t_sec, value) 序列\n");

    // 2b-2) 有界读取：cap 是硬上限，绝不返回超过 cap 的点（kernel 内等步长抽稀）。
    const auto capped = ds->channel(QStringLiteral("demo.roll"), /*cap=*/2);
    assert(capped.has_value() && capped->size() == 2);
    fprintf(stdout, "[ok] readWindow cap 硬上限：请求 2 点只读 2 点（不全量物化）\n");

    // 2c) Rust kernel 是数据地基：C++ 只持句柄，算子与缓存全部委托 Rust C ABI。
    assert(ds->nativeHandle() != nullptr); // 数据本体由 Rust PsDataset 持有
    // LOD 抽稀：把 3 点压到 2 个像素桶，Rust 返回 (x,min,max) 三元组。
    const auto lod = ds->lodMinMax(QStringLiteral("demo"), QStringLiteral("roll"),
                                   0, 2000000, 2);
    assert(!lod.isEmpty() && lod.size() <= 2);
    fprintf(stdout, "[ok] LOD 抽稀在 Rust 侧完成（%d 个三元组）\n", int(lod.size()));
    // 统计算子（ps_stats_series）：roll = 1.0/1.5/2.0，均值 1.5、最小 1、最大 2。
    const auto st = ds->stats(ch->value, /*withMedian=*/true);
    assert(st.has_value());
    assert(st->count == 3);
    assert(std::fabs(st->mean - 1.5) < 1e-9);
    assert(std::fabs(st->min - 1.0) < 1e-9 && std::fabs(st->max - 2.0) < 1e-9);
    fprintf(stdout, "[ok] 统计/滤波/抽取算子委托 Rust ps-ops（mean=%.2f）\n", st->mean);
    // 抽取（ps_decimate_series）：factor=2 点数约减半。
    QVector<double> dt, dv;
    const int decRc = ds->decimate(2, ch->time, ch->value, dt, dv); // 副作用不能放进 assert（NDEBUG 会消除）
    assert(decRc == 0);
    assert(dt.size() < ch->size() && dt.size() >= 1);
    fprintf(stdout, "[ok] 抗混叠抽取 factor=2：%d -> %d 点\n", int(ch->size()), int(dt.size()));

    // 2d) 缓存（ps-cache）：同一数据集写盘后按源文件指纹还原，schema 与列值一致。
    {
        const QString cachePath = csv + QStringLiteral(".psc");
        const QFileInfo fi(csv);
        const bool cacheSaved = ds->saveCache(cachePath, fi.size(), fi.lastModified().toMSecsSinceEpoch());
        assert(cacheSaved);
        QString cacheErr;
        auto restored = Dataset::loadCache(cachePath, fi.size(),
                                           fi.lastModified().toMSecsSinceEpoch(), &cacheErr);
        assert(restored.has_value());
        assert(restored->nativeHandle() != nullptr);
        assert(restored->tableCount() == 1);
        const auto rch = restored->channel(QStringLiteral("demo.roll"));
        assert(rch.has_value() && rch->size() == 3);
        assert(std::fabs(rch->value.at(1) - 1.5) < 1e-9);
        // 指纹不匹配应过期（size+1）。
        auto stale = Dataset::loadCache(cachePath, fi.size() + 1,
                                        fi.lastModified().toMSecsSinceEpoch());
        assert(!stale.has_value());
        fprintf(stdout, "[ok] ps-cache 指纹命中还原 + 指纹过期失效\n");
    }

    // 2e) ULog 扩展：复用旧 extension 的协议算法，流式灌入 Rust typed 表。
    //     合成 9000 行（跨 kChunkRows=4096 三批），验证多表 / typed 列 / 微秒时间轴 / 缓存。
    {
        qint64 firstUs = 0, lastUs = 0;
        const int rows = 9000;
        const QString ulg = writeUlog(dir, rows, &firstUs, &lastUs);
        Intent ui;
        ui.op = QStringLiteral("data.load");
        ui.params[QStringLiteral("path")] = ulg;
        ui.params[QStringLiteral("format")] = QStringLiteral("ulog");
        Result<IntentResult> uloaded = application.host().execute(ui);
        if (!uloaded)
            fprintf(stderr, "[diag] ULog 加载失败: %s / %s\n",
                    uloaded.error().code.toUtf8().constData(),
                    uloaded.error().message.toUtf8().constData());
        assert(uloaded);
        const DatasetId uid = uloaded.value().payload.toInt();
        const Dataset *uds = application.data().get(uid);
        assert(uds != nullptr);
        assert(uds->format() == QStringLiteral("ulog"));

        // 归并的**行为锁**：ULog 必须走统一摄取管道，
        // 于是数据集名是管道生成的 "src<源id>_<解析器id>"，
        // 而不是旧路径里驱动自己起的名字（文件名）。
        // 这条断言的意义：将来谁把 ULog 退回"驱动自己读文件"，测试立刻红。
        assert(uds->name().startsWith(QStringLiteral("src")));
        assert(uds->name().endsWith(QStringLiteral("_ulog")));

        // 多张 typed 表：sensor 数值表 + events + facts 元数据表
        if (uds->tableCount() < 3)
        {
            fprintf(stderr, "[diag] ULog 表数=%d:", uds->tableCount());
            for (int i = 0; i < uds->tableCount(); ++i)
                fprintf(stderr, " %s", uds->table(i)->name.toUtf8().constData());
            fprintf(stderr, "\n");
        }
        assert(uds->tableCount() >= 3);
        const Table *sensor = uds->table(QStringLiteral("sensor"));
        assert(sensor != nullptr);
        assert(sensor->rowCount() == rows);
        // typed 列：x=F32、y=U16（协议算法保留原始字段类型）
        const Column *cx = sensor->column(QStringLiteral("x"));
        const Column *cy = sensor->column(QStringLiteral("y"));
        assert(cx != nullptr && cx->type == ValueType::F32);
        assert(cy != nullptr && cy->type == ValueType::U16);
        // timestamp 走原生 U64 微秒，不经 double 中转——首尾时间精确
        assert(sensor->firstTimeUs == firstUs);
        assert(sensor->lastTimeUs == lastUs);
        fprintf(stdout, "[ok] ULog 流式加载 %d 行（跨 3 批）：多表 + typed 列 F32/U16 + 微秒时间轴精确\n", rows);

        // 全量取值核对（cap 提到行数）：x=i*0.5、y=i%1000，首行/末行精确
        const auto xview = uds->channel(QStringLiteral("sensor"), QStringLiteral("x"), rows);
        assert(xview.has_value() && xview->size() == rows);
        assert(std::fabs(xview->value.first() - 0.0) < 1e-5);
        assert(std::fabs(xview->value.last() - (rows - 1) * 0.5) < 1e-3);
        const auto yview = uds->channel(QStringLiteral("sensor"), QStringLiteral("y"), rows);
        assert(yview.has_value() && yview->size() == rows);
        assert(yview->value.last() == (rows - 1) % 1000);
        fprintf(stdout, "[ok] ULog typed 列值精确（F32 x / U16 y 跨批尾批 flush 正确）\n");

        // 元数据小表：events（L 段）与 facts（头信息）
        assert(uds->table(QStringLiteral("events")) != nullptr);
        assert(uds->table(QStringLiteral("events"))->rowCount() == 1);
        assert(uds->table(QStringLiteral("facts")) != nullptr);
        fprintf(stdout, "[ok] ULog 元数据段 L/I 落表（events/facts）\n");

        // 缓存命中：format 改为 ulog 还原，schema 一致
        {
            const QFileInfo fi(ulg);
            QString ce;
            // 缓存按 (源文件, 解析器) 粒度落盘：<source>.<parserId>.psc。
            // 同一份源喂多个解析器时各有各的缓存，不会互相覆盖。
            auto restored = Dataset::loadCache(ulg + QStringLiteral(".ulog.psc"),
                                               fi.size(), fi.lastModified().toMSecsSinceEpoch(), &ce);
            assert(restored.has_value());
            assert(restored->table(QStringLiteral("sensor")) != nullptr);
            assert(restored->table(QStringLiteral("sensor"))->rowCount() == rows);
            fprintf(stdout, "[ok] ULog 数据集 ps-cache 指纹命中还原\n");
        }
    }

    // 3) 意图执行：plot 模块按 table.column 编译出 PlotSpec
    Result<IntentResult> rendered = application.host().execute(renderIntent(id, QStringLiteral("demo.pitch")));
    assert(rendered);
    const QVariantMap spec = rendered.value().payload.toMap();
    assert(spec.value(QStringLiteral("type")).toString() == QStringLiteral("timeseries"));
    assert(spec.value(QStringLiteral("column")).toString() == QStringLiteral("pitch"));
    assert(spec.value(QStringLiteral("valueType")).toString() == QStringLiteral("F64"));
    fprintf(stdout, "[ok] 意图 plot.render 产出 PlotSpec（含 table/column/valueType）\n");

    // 3b) 脚本自动化：Lua 通过门面 ps.* 发意图，与 GUI/CLI 走同一执行器。
    //     脚本里不认识 CSV/plot 模块，只认识 ps 门面。
    {
        const QString script =
            QStringLiteral("local id = ps.data.load('%1', 'csv', 100)\n"
                           "ps.log('script loaded dataset ' .. id)\n"
                           "local spec = ps.plot.render(id, 'demo.roll')\n"
                           "assert(spec.type == 'timeseries')\n"
                           "assert(spec.column == 'roll')\n").arg(csv);
        Intent runScript;
        runScript.op = QStringLiteral("script.run");
        runScript.params[QStringLiteral("source")] = script.toUtf8();

        application.recorder().clear();
        application.recorder().setRecording(true);
        Result<IntentResult> sr = application.host().execute(runScript);
        application.recorder().setRecording(false);
        assert(sr);
        fprintf(stdout, "[ok] Lua 脚本通过 ps 门面执行成功（headless，无 QWidget）\n");

        // 3c) 录制：脚本调用产生的业务意图被录成 Job（不含 script.run 本身）
        assert(application.recorder().size() == 2);
        const Job recorded = application.recorder().toJob();
        assert(recorded.at(0).op == QStringLiteral("data.load"));
        assert(recorded.at(1).op == QStringLiteral("plot.render"));
        fprintf(stdout, "[ok] 脚本操作被录制成 2 条业务意图的可回放 Job\n");

        // 3d) 回放：移除脚本数据集后重放 Job，得到相同 PlotSpec —— 闭环成立。
        //     稳定 id=100 让回放与会话状态无关，不影响主流程的 id=0 数据集。
        application.data().remove(100);
        assert(application.data().get(100) == nullptr);
        assert(application.data().get(id) != nullptr); // 主数据集仍在
        int replayedId = -1;
        QVariantMap replayedSpec;
        for (int i = 0; i < recorded.size(); ++i)
        {
            Result<IntentResult> rr = application.host().execute(recorded.at(i));
            assert(rr);
            if (recorded.at(i).op == QStringLiteral("data.load"))
                replayedId = rr.value().payload.toInt();
            else
                replayedSpec = rr.value().payload.toMap();
        }
        assert(replayedId >= 0);
        assert(replayedSpec.value(QStringLiteral("column")).toString() == QStringLiteral("roll"));
        assert(replayedSpec.value(QStringLiteral("points")).toInt() == 3);
        fprintf(stdout, "[ok] Job 回放出相同 PlotSpec —— 脚本录制/回放闭环成立\n");
    }

    // 3c) data 派生能力（对应老 datasource 的 DataSourceCommands）：
    //     recent 自动记录 / export 导出活动通道 / clearCache / reload。
    //     全部走宿主系统级 handler，headless 下也能执行。
    {
        auto mk = [](const QString &op) { Intent i; i.op = op; return i; };
        // data.load 成功后自动记入最近文件（settings 持久化）。
        Intent recent = mk(QStringLiteral("data.recent"));
        Result<IntentResult> rr = application.host().execute(recent);
        CHECK(rr);
        const QStringList recents = rr.value().payload.toStringList();
        CHECK(recents.contains(csv));
        fprintf(stdout, "[ok] data.recent：data.load 成功后最近文件自动记录并可读\n");

        // data.export：把 CSV 数据集的活动列导出成新 CSV 文件。
        const QString outCsv = dir.filePath(QStringLiteral("exported.csv"));
        Intent exp = mk(QStringLiteral("data.export"));
        exp.params[QStringLiteral("dataset")] = id;
        exp.params[QStringLiteral("path")] = outCsv;
        Result<IntentResult> er = application.host().execute(exp);
        if (!er)
            fprintf(stderr, "[diag] data.export 失败: %s / %s\n",
                    er.error().code.toUtf8().constData(),
                    er.error().message.toUtf8().constData());
        CHECK(er);
        CHECK(QFile::exists(outCsv));
        { QFile f(outCsv);
          CHECK(f.open(QIODevice::ReadOnly | QIODevice::Text));
          CHECK(QString::fromUtf8(f.readAll()).contains(QStringLiteral("time")));
          f.close(); }
        fprintf(stdout, "[ok] data.export：经 Dataset::channel 取数写出 CSV（有界抽稀）\n");

        // data.reload：重载活动源，重新走 data.load（有 sources 支撑）。
        application.context().setActiveSourceId(application.context().sources().ids().first());
        Intent rel = mk(QStringLiteral("data.reload"));
        Result<IntentResult> rer = application.host().execute(rel);
        if (!rer)
            fprintf(stderr, "[diag] data.reload 失败: %s / %s\n",
                    rer.error().code.toUtf8().constData(),
                    rer.error().message.toUtf8().constData());
        CHECK(rer);
        fprintf(stdout, "[ok] data.reload：活动源经 data.load 同一路径重载\n");

        // data.clearCache：删除 .psc 缓存（无缓存时返回 0 也算成功）。
        Intent cc = mk(QStringLiteral("data.clearCache"));
        Result<IntentResult> cr = application.host().execute(cc);
        CHECK(cr);
        CHECK(cr.value().payload.toInt() >= 0);
        fprintf(stdout, "[ok] data.clearCache：解析缓存清理成功（清掉 %d 个）\n",
                cr.value().payload.toInt());

        // data.remove：无效 id 明确失败（不误删真实源）；有效路径在 GUI 垃圾桶处验证。
        {
            Intent rr = mk(QStringLiteral("data.remove"));
            rr.params[QStringLiteral("id")] = -9999;
            Result<IntentResult> rm = application.host().execute(rr);
            CHECK(!rm);
            fprintf(stdout, "[ok] data.remove：无效源 id 明确失败，不误删\n");
        }

        // --- 工作区数据源配置：data.load 自动记录 + workspace 意图 ---
        {
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
            // data.load（上面已成功加载 csv）应已把来源记入 ws/data/sources。
            WorkspaceDataConfig cfg = WorkspaceDataConfig::load(application.context().settings());
            CHECK(!cfg.sources.isEmpty());
            const SourceSpec *rec = cfg.findByOrigin(csv);
            CHECK(rec != nullptr);
            CHECK(rec->kind == QStringLiteral("file"));
            CHECK(!rec->parserIds.isEmpty());
            fprintf(stdout, "[ok] data.load 成功后自动记入工作区配置（来源+解析器）\n");

            // 序列化往返：toJson -> fromJson 后字段不丢。
            const QJsonObject blob = cfg.toJson();
            const WorkspaceDataConfig round = WorkspaceDataConfig::fromJson(blob);
            CHECK(round.sources.size() == cfg.sources.size());
            CHECK(round.findByOrigin(csv) != nullptr);
            fprintf(stdout, "[ok] WorkspaceDataConfig JSON 往返：字段不丢\n");

            // 手动加一条派生配方（只存配方，不存结果数组），再保存/读取。
            SourceSpec withRecipe = *cfg.findByOrigin(csv);
            DeriveRecipeSpec dr;
            dr.sourceOrigin = csv;
            dr.table = QStringLiteral("csv");
            dr.column = QStringLiteral("value");
            dr.filter = QStringLiteral("absolute");
            dr.paramsJson = QStringLiteral("{}");
            withRecipe.deriveRecipes << dr;
            cfg.upsert(withRecipe);
            CHECK(cfg.save(application.context().settings()));
            const WorkspaceDataConfig reread = WorkspaceDataConfig::load(application.context().settings());
            CHECK(reread.findByOrigin(csv) != nullptr);
            CHECK(reread.findByOrigin(csv)->deriveRecipes.size() == 1);
            // 关键：配置里只有配方字符串，没有任何时间/数值数组（不存数据本体）。
            const QJsonObject sourceJson = reread.findByOrigin(csv)->toJson();
            CHECK(!sourceJson.contains(QStringLiteral("time")));
            CHECK(!sourceJson.contains(QStringLiteral("value")));
            CHECK(sourceJson.contains(QStringLiteral("deriveRecipes")));
            fprintf(stdout, "[ok] 派生配方只存算子+参数，不存结果数组\n");

            // data.workspace.list / save：可读、可 flush。
            Result<IntentResult> wl = application.host().execute(mk(QStringLiteral("data.workspace.list")));
            CHECK(wl);
            CHECK(!wl.value().payload.toByteArray().isEmpty());
            Result<IntentResult> ws = application.host().execute(mk(QStringLiteral("data.workspace.save")));
            CHECK(ws);
            fprintf(stdout, "[ok] data.workspace.list/save：配置可读可落盘\n");

            // data.workspace.restore：重放 data.load（record=false 不重复记录）。
            const int sourcesBefore = cfg.sources.size();
            Result<IntentResult> wr = application.host().execute(mk(QStringLiteral("data.workspace.restore")));
            CHECK(wr);
            const QVariantMap stat = wr.value().payload.toMap();
            CHECK(stat.value(QStringLiteral("sources")).toInt() >= 1);
            // 恢复不能把源重复记进配置（record=false 生效）。
            const WorkspaceDataConfig afterRestore =
                WorkspaceDataConfig::load(application.context().settings());
            CHECK(afterRestore.sources.size() == sourcesBefore);
            fprintf(stdout, "[ok] data.workspace.restore：重放 data.load，不重复记录源\n");
        }
    }

    // 4) 卸载隔离：关掉绘图模块后，CSV 仍能加载，数据不消失
    CHECK(application.host().deactivate(QStringLiteral("mod.plot")));
    CHECK(!application.host().isActive(QStringLiteral("mod.plot")));
    Result<IntentResult> stillLoads = application.host().execute(loadIntent(csv));
    assert(stillLoads);
    assert(application.data().get(id) != nullptr);
    fprintf(stdout, "[ok] 卸载 plot 模块后 CSV 仍可用，数据未丢失\n");

    // 5) 从工具箱撤销 CSV 驱动后，data.load format=csv 不再可执行（能力随注册表撤销）。
    //    注意：撤销驱动 ≠ 卸载器官，器官三个模块仍在。
    CHECK(toolbox.unregisterDriver(QStringLiteral("csv")));
    CHECK(!toolbox.contains(QStringLiteral("csv")));
    CHECK(toolbox.count() == 1);
    Result<IntentResult> noHandler = application.host().execute(loadIntent(csv));
    CHECK(!noHandler);
    CHECK(noHandler.error().code == QStringLiteral("eformat"));
    fprintf(stdout, "[ok] 从工具箱撤销 CSV 驱动后能力被精确撤销（eformat）\n");

    // 5b) 工具箱隔离：撤掉 CSV 不影响 ULog；再撤 ULog 后 ulog 能力精确撤销。
    {
        qint64 firstUs = 0, lastUs = 0;
        const QString ulg = writeUlog(dir, 6000, &firstUs, &lastUs);
        Intent ui;
        ui.op = QStringLiteral("data.load");
        ui.params[QStringLiteral("path")] = ulg;
        ui.params[QStringLiteral("format")] = QStringLiteral("ulog");
        Result<IntentResult> stillLoads = application.host().execute(ui); // CSV 已撤，ULog 仍在
        CHECK(stillLoads);
        CHECK(toolbox.unregisterDriver(QStringLiteral("ulog")));
        // 注意：撤销 csv 时已按包原子撤掉 pack.common（csv + custom-protocol），
        // 这里无需也不能再单独撤 custom-protocol。
        Result<IntentResult> noUlog = application.host().execute(ui);
        CHECK(!noUlog && noUlog.error().code == QStringLiteral("eformat"));
        CHECK(toolbox.count() == 0);
        fprintf(stdout, "[ok] 工具箱隔离：撤 CSV 不影响 ULog；撤 ULog 后 ulog 能力精确撤销\n");

        // 重新自注册验证可逆（工具箱可热插，不留悬空能力）。
        nextpilot::plotscope::ext::registerBuiltinFormats(toolbox);
        CHECK(toolbox.count() == 3); // csv + ulog + custom-protocol
    }

    // 6) Job 序列化往返
    Job job;
    job.append(loadIntent(csv));
    job.append(renderIntent(id, QStringLiteral("demo.roll")));
    Result<void> parseErr;
    Job restored = Job::fromJson(job.toJson(), &parseErr);
    assert(parseErr);
    assert(restored.size() == 2);
    fprintf(stdout, "[ok] Job JSON 序列化往返\n");

    // 7) 百万行压力：验证流式加载 + 有界读取，C++ 侧内存与行数无关。
    //    用 20 万行（CI 友好）即可证明三条不变量；行数可放大到百万而不改代码路径。
    //    直接走 Dataset::appendFrame 分块灌入，模拟 CSV 扩展的加载方式。
    {
        constexpr qint64 kRows = 200000;
        constexpr int kBatch = 4096;
        Dataset big = Dataset::create(QStringLiteral("big"), QStringLiteral("stress"));
        QVector<TimeUs> tbuf(kBatch);
        QVector<double> vbuf(kBatch);
        QVector<FrameColumn> cols(1);
        cols[0].name = QStringLiteral("sig");
        cols[0].unit = QString();
        cols[0].type = ValueType::F64;
        cols[0].values = vbuf.constData();
        cols[0].nulls = nullptr;

        qint64 written = 0;
        while (written < kRows)
        {
            const int n = static_cast<int>(qMin<qint64>(kBatch, kRows - written));
            for (int i = 0; i < n; ++i)
            {
                const qint64 r = written + i;
                tbuf[i] = static_cast<TimeUs>(r) * 1000; // 每行 1ms → 微秒
                vbuf[i] = static_cast<double>(r % 1000);
            }
            QString err;
            const bool appended = big.appendFrame(QStringLiteral("trace"), tbuf.constData(), n, cols, &err);
            assert(appended && err.isEmpty());
            (void)appended; // Release(NDEBUG) 下 assert 为空，避免 unused 警告
            written += n;
        }
        big.refreshSchema();

        const Table *bt = big.table(QStringLiteral("trace"));
        assert(bt != nullptr && bt->rows == kRows);
        // 拷成局部值：Table* 指向 Dataset 内部 QVector，后续 channel() 等调用后不能假设仍有效。
        const TimeUs traceFirstUs = bt->firstTimeUs;
        const TimeUs traceLastUs = bt->lastTimeUs;
        assert(traceFirstUs == 0);
        assert(traceLastUs == (kRows - 1) * 1000);
        fprintf(stdout, "[ok] 流式灌入 %lld 行：schema 只存首尾时间，无 C++ 全量副本\n", kRows);

        // 有界读取：无论 20 万行，channel 默认只取 kDefaultWindowPoints（4096）点。
        const auto w = big.channel(QStringLiteral("trace.sig"));
        assert(w.has_value() && w->size() <= Dataset::kDefaultWindowPoints);
        assert(w->size() > 0);
        // 自定义更小的 cap 也严格生效。
        const auto w2 = big.channel(QStringLiteral("trace.sig"), /*cap=*/128);
        assert(w2.has_value() && w2->size() == 128);
        fprintf(stdout, "[ok] 20 万行有界读取：默认 %d 点 / 指定 128 点（与行数无关）\n",
                Dataset::kDefaultWindowPoints);

        // LOD：20 万行压到 800 个像素桶，三元组数恒定在 px 量级。
        const auto blod = big.lodMinMax(QStringLiteral("trace"), QStringLiteral("sig"),
                                        traceFirstUs, traceLastUs, /*px=*/800);
        assert(!blod.isEmpty() && blod.size() <= 800);
        fprintf(stdout, "[ok] 20 万行 LOD 抽稀到 %d 个三元组（px 有界）\n", int(blod.size()));
    }

    // ========================================================================
    // 协议解析器（kind=message）：自述 + configure + parse 的契约
    // ========================================================================
    {
        // NMEA 已随 gnss 包自注册进工具箱；注册表按 kind=message 可查。
        const QList<IMessageParser *> parsers = toolbox.messageParsers();
        CHECK(parsers.size() >= 1);
        IMessageParser *nmea = toolbox.messageParser(QStringLiteral("nmea"));
        CHECK(nmea != nullptr);
        CHECK(nmea->kind() == ContributionKind::Message);

        // 自述四要素：协议名 / 使用范围（领域+用途）/ 支持 transfer / 配置 schema。
        CHECK(nmea->protocolName() == QStringLiteral("NMEA 0183"));
        CHECK(nmea->domain() == Domain::Gnss);
        CHECK(!nmea->usage().isEmpty());
        CHECK(nmea->supportedTransports().contains(MessageTransport::Serial));
        CHECK(nmea->supportedTransports().contains(MessageTransport::Udp));
        CHECK(nmea->configSchema().value(QStringLiteral("fields")).toArray().size() >= 1);
        // 按 transport 反查（对话框/CLI 据此过滤）。
        CHECK(toolbox.messageParsersByTransport(MessageTransport::Serial).contains(nmea));
        CHECK(toolbox.messageParserForProtocol(QStringLiteral("nmea")) == nmea);
        fprintf(stdout, "[ok] 协议解析器自述：协议名/领域+用途/transfer/schema 四要素齐全，注册表可查\n");

        // 直接实例化一个独立样本：默认内置模板可用（无 configure 也能 parse）。
        nextpilot::plotscope::ext::NmeaMessageParser p;
        CHECK(p.templateCount() >= 3);
        // 标准 GGA 句（校验和 47 与 payload 自洽）。
        const QByteArray gga =
            "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
        DecodedFrame f;
        CHECK(p.parse(gga, f));
        CHECK(f.protocol == QStringLiteral("nmea"));
        CHECK(f.frameName == QStringLiteral("GGA"));
        CHECK(f.checksumOk);
        CHECK(!f.fields.isEmpty());

        // configure：从传入参数取 database_path，不读全局。
        // 用临时文件放一个只含 RMC 的精简模板，configure 后应只认 RMC。
        QTemporaryDir tplDir;
        const QString tpl = tplDir.path() + QStringLiteral("/tpl.json");
        {
            QFile out(tpl);
            CHECK(out.open(QIODevice::WriteOnly | QIODevice::Text));
            const QByteArray json =
                R"({"sentences":[{"key":"RMC","fields":[{"name":"speed","index":7,"type":"number","label":"Speed"}]}]})";
            out.write(json);
            out.close();
        }
        QJsonObject params;
        params.insert(QStringLiteral("database_path"), tpl);
        CHECK(p.configure(params, nullptr));
        // parse GGA → 模板已经没有 GGA，应标记 unknown。
        DecodedFrame f2;
        CHECK(p.parse(gga, f2));
        CHECK(f2.unknown);
        // parse RMC → 能用新模板解出 speed 字段。
        const QByteArray rmc =
            "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,,,*3D\r\n";
        DecodedFrame f3;
        CHECK(p.parse(rmc, f3));
        CHECK(!f3.unknown);
        bool sawSpeed = false;
        for (const DecodedField &d : f3.fields)
            if (d.name == QStringLiteral("speed"))
                sawSpeed = true;
        CHECK(sawSpeed);
        fprintf(stdout, "[ok] 协议 configure：只从传入 database_path 取模板（不读全局），parse 随模板切换\n");

        // param() 点分嵌套 + 缺省回退：不存在的键返回 fallback，绝不取全局。
        QJsonObject nested;
        nested.insert(QStringLiteral("time"),
                      QJsonObject{{QStringLiteral("mode"), QStringLiteral("column")}});
        CHECK(p.configure(nested, nullptr));
        CHECK(p.param(QStringLiteral("time.mode")).toString() == QStringLiteral("column"));
        CHECK(p.param(QStringLiteral("missing.key"), QStringLiteral("fallback")).toString()
              == QStringLiteral("fallback"));
        fprintf(stdout, "[ok] 协议参数：param() 支持点分嵌套 + 缺省回退，参数只在实例内生效\n");
    }

    fprintf(stdout, "\n全部架构契约测试通过\n");
    return 0;
}

#include "test_architecture.moc"
