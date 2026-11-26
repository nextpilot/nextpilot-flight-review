#include "PlotViewFactories.h"

#include "ContributionParser.h"
#include "IIntentExecutor.h"
#include "InstantiationService.h"
#include "InstrumentView.h"
#include "PlotThumbnails.h"
#include "PlotWorkspace.h"
#include "PropertyPanel.h"
#include "ViewRegistry.h"

#include "AppContext.h"

#include <QHash>
#include <QPointer>

#include <memory>

// plot 视图装配·注册半（A2c）：声明在此落（owner "plot"），provider 登记也在此。
// 不写 using namespace dataset|parser（A2c 判据：gui 面 0）。

namespace nextpilot::plotscope::plot {

namespace {

// ---- plot.workspace editor provider ----
//
// ResourceRef 构成（A2c PR 必答：viewType → scheme → uri 表；uri 语义归 owner——
// plot 模块自己定义并消费，壳不解析）：
//   scheme  : plot://
//   uri     : plot://workspace（工作区单例：同 ref 复用同一页签）
//   mime    : application/x-plot-workspace
//   title   : 绘图工作区
//
// 复用语义（§7 判据）：同一 ref（uri 相等）复用同一页签（provider 内 uri → 面板
// 缓存，destroyed 兜底清空）；不同 ref 各开一页签——EditorServiceAdapter 每次都调
// createEditor，复用归 provider（2a 定死的语义分层）。
class WorkspaceEditorProvider final : public nextpilot::plotscope::registry::IEditorProvider
{
public:
    WorkspaceEditorProvider(nextpilot::plotscope::context::AppContext &appCtx,
                            nextpilot::plotscope::intent::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor)
    {
    }

    bool canOpen(const nextpilot::plotscope::registry::ResourceRef &ref) const override
    {
        return ref.uri == QLatin1String("plot://workspace");
    }

    QWidget *createEditor(const nextpilot::plotscope::registry::ResourceRef &ref, QWidget *parent) override
    {
        Q_UNUSED(ref);
        // 缓存命中（同一 ref）：复用既有页签，不重复建。
        auto it = m_open.find(ref.uri);
        if (it != m_open.end() && !it.value().isNull())
            return it.value().data();

        auto *panel = new nextpilot::plotscope::gui::PlotWorkspace(m_app, m_executor, parent);
        m_open.insert(ref.uri, QPointer<QWidget>(panel));
        QObject::connect(panel, &QObject::destroyed, panel, [this, uri = ref.uri]() { m_open.remove(uri); });
        return panel;
    }

    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    QHash<QString, QPointer<QWidget>> m_open; // uri → 已建页签（同一 ref 复用）
};

// ---- plot.instrument editor provider ----
//
// ResourceRef 构成：
//   scheme  : plot://
//   uri     : plot://instrument/<kind>（kind=仪表视图种类；不同 kind 各一页签）
//   mime    : application/x-plot-instrument
//   title   : #<kind>
//
// kind 的取数/渲染语义归 InstrumentView（面板创建后经 plot.instrument.setKind
// viewAction 切换——与 Ribbon gallery、脚本共用同一条意图路径）。
class InstrumentEditorProvider final : public nextpilot::plotscope::registry::IEditorProvider
{
public:
    InstrumentEditorProvider(nextpilot::plotscope::context::AppContext &appCtx,
                             nextpilot::plotscope::intent::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor)
    {
    }

    bool canOpen(const nextpilot::plotscope::registry::ResourceRef &ref) const override
    {
        return ref.uri.startsWith(QLatin1String("plot://instrument/"));
    }

    QWidget *createEditor(const nextpilot::plotscope::registry::ResourceRef &ref, QWidget *parent) override
    {
        // 缓存命中（同一 kind）：复用既有页签，不重复建。
        auto it = m_open.find(ref.uri);
        if (it != m_open.end() && !it.value().isNull())
            return it.value().data();

        // 解析 uri（owner 语义：plot://instrument/<kind>）。
        const QString kind = ref.uri.mid(QLatin1String("plot://instrument/").size());
        if (kind.isEmpty())
            return nullptr; // 无 kind 的仪表面板无意义，明确拒开（不静默吞页签）

        // 经工厂创建：setKind viewAction 处理器 / 活动通道联动都在工厂里接好
        // （createInstrumentPanel），provider 不重复布线。
        QWidget *panel = nextpilot::plotscope::gui::createInstrumentPanel(m_app, m_executor, parent);
        m_open.insert(ref.uri, QPointer<QWidget>(panel));
        QObject::connect(panel, &QObject::destroyed, panel, [this, uri = ref.uri]() { m_open.remove(uri); });

        // 面板创建时已登记 plot.instrument.setKind 处理器（createInstrumentPanel），
        // 这里按 ref.kind 切到对应图型——openEditor 的资源语义一次到位。
        QVariantMap ip;
        ip.insert(QStringLiteral("kind"), kind);
        QVariant ivr;
        nextpilot::plotscope::intent::Result<QVariant> ires(ivr);
        m_app.interactions().dispatchViewAction(QStringLiteral("plot.instrument.setKind"), ip, ires);
        return panel;
    }

private:
    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    QHash<QString, QPointer<QWidget>> m_open; // uri → 已建页签（同一 ref 复用）
};

// 借用指针的所有权（ViewRegistry 只借用不 delete；进程存续期持有）。
std::unique_ptr<WorkspaceEditorProvider> &workspaceProvider()
{
    static std::unique_ptr<WorkspaceEditorProvider> s_provider;
    return s_provider;
}
std::unique_ptr<InstrumentEditorProvider> &instrumentProvider()
{
    static std::unique_ptr<InstrumentEditorProvider> s_provider;
    return s_provider;
}

} // namespace

bool installPlotViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                              intent::IIntentExecutor *executor)
{
    if (!services || !appCtx || !executor)
        return false;
    auto *views = services->getService<nextpilot::plotscope::registry::ViewRegistry>(QStringLiteral("views.registry"));
    if (!views)
        return false;

    const QString owner = QStringLiteral("plot");

    // ---- 呈现侧三件（A2c）：缩略图 QIcon 注册进 IconRegistry ----
    // 对位 VSCode "扩展激活时注册图标"；壳只 iconRegistry.icon(id)，不持有绘制器。
    // 幂等（IconRegistry 先到者为准），装配重入安全。
    nextpilot::plotscope::gui::PlotThumbnails::registerIcons();

    // ---- 声明半（owner "plot"）：views + customEditors，先于 provider 登记 ----
    // registerEditorProvider 要求 viewType 已声明且 owner 一致（2a 防冒名线），
    // 模块 initialize 晚于本调用（host.activate 在 build 内才跑），声明在此落。
    // 声明代码物理在本 TU（modules/plot/gui，plot 模块目录）——plot 自声明，
    // 壳不代声明（A2c 打回线 1）；壳只负责在本函数被调用时提供 services。
    {
        nextpilot::plotscope::registry::ContributesDoc doc;
        doc.owner = owner;

        nextpilot::plotscope::registry::ContributedViewContainer sidebar;
        sidebar.id = QStringLiteral("plot.sidebar");
        sidebar.title = QStringLiteral("绘图");
        sidebar.area = QStringLiteral("sidebar"); // 壳落区映射：sidebar → PrimarySidebar
        sidebar.order = 1;
        doc.viewContainers.append(sidebar);

        // property-panel 归位（A2c）：view id = plot.properties（pageKey 同步改，
        // toggle 断言跟着改 id——哑开关不复发的判据）。
        nextpilot::plotscope::registry::ContributedView view;
        view.container = QStringLiteral("plot.sidebar");
        view.id = QStringLiteral("plot.properties");
        view.name = QStringLiteral("属性");
        view.icon = QStringLiteral("property");
        view.order = 0;
        doc.views.append(view);

        nextpilot::plotscope::registry::ContributedCustomEditor instrument;
        instrument.viewType = QStringLiteral("plot.instrument");
        instrument.displayName = QStringLiteral("仪表");
        instrument.priority = 0;
        doc.customEditors.append(instrument);

        nextpilot::plotscope::registry::ContributedCustomEditor workspace;
        workspace.viewType = QStringLiteral("plot.workspace");
        workspace.displayName = QStringLiteral("绘图工作区");
        workspace.priority = 0;
        doc.customEditors.append(workspace);

        views->declare(doc);
    }

    // ---- 注册半 ----
    bool ok = true;
    // plot.properties（A2c 表 A：sidebar；PropertyPanel 已迁 modules/plot/gui）。
    ok &= views->registerViewProvider(QStringLiteral("plot.properties"),
                                      &nextpilot::plotscope::gui::createPropertyPanel, owner);
    // plot.instrument（editor；声明已在本函数落）。
    if (!instrumentProvider())
        instrumentProvider().reset(new InstrumentEditorProvider(*appCtx, *executor));
    ok &=
        views->registerEditorProvider(QStringLiteral("plot.instrument"), instrumentProvider().get(), owner) != nullptr;
    // plot.workspace（editor；ActivitySpec.centralRef=plot://workspace 由壳透传打开）。
    if (!workspaceProvider())
        workspaceProvider().reset(new WorkspaceEditorProvider(*appCtx, *executor));
    ok &= views->registerEditorProvider(QStringLiteral("plot.workspace"), workspaceProvider().get(), owner) != nullptr;
    return ok;
}

} // namespace nextpilot::plotscope::plot
