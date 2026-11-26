#include "DataViewFactories.h"

#include "ContributionParser.h"
#include "DataTablePanel.h"
#include "IIntentExecutor.h"
#include "InstantiationService.h"
#include "ViewRegistry.h"

#include "AppContext.h"
#include "DataManagerPanel.h"

#include <QHash>
#include <QPointer>

#include <memory>

// data 视图装配·注册半（A2b 笔2b 第③步）：声明在 DataModule::initialize（owner
// "data"），provider 登记在此。不写 using namespace（笔 5 判据：模块面 0）。

namespace nextpilot::plotscope::data {

namespace {

// ---- data.table editor provider ----
//
// ResourceRef 构成（§7 PR 必答 1；uri 语义归 owner——data 模块自己定义并消费）：
//   scheme  : dataset://
//   uri     : dataset://<datasetId>/<tableName>
//   mime    : application/x-dataset-table
//   title   : #<datasetId>/<tableName>
//
// 复用语义（§7 判据）：同一 ref（uri 相等）复用同一页签（provider 内 uri → 面板
// 缓存，destroyed 兜底清空）；不同 ref 各开一页签——EditorServiceAdapter 每次都调
// createEditor，复用归 provider（2a 定死的语义分层）。
class DataEditorProvider final : public nextpilot::plotscope::registry::IEditorProvider
{
public:
    DataEditorProvider(nextpilot::plotscope::context::AppContext &appCtx,
                       nextpilot::plotscope::intent::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor)
    {
    }

    bool canOpen(const nextpilot::plotscope::registry::ResourceRef &ref) const override
    {
        return ref.uri.startsWith(QLatin1String("dataset://"));
    }

    QWidget *createEditor(const nextpilot::plotscope::registry::ResourceRef &ref, QWidget *parent) override
    {
        // 缓存命中（同一 ref）：复用既有页签，不重复建。
        auto it = m_open.find(ref.uri);
        if (it != m_open.end() && !it.value().isNull())
            return it.value().data();

        // 解析 uri（owner 语义：dataset://<datasetId>/<table>）；解析失败返回空面板
        // 由动作通道出诊断——provider 不静默吞掉整页签。
        const QString body = ref.uri.mid(QLatin1String("dataset://").size());
        const int slash = body.indexOf(QLatin1Char('/'));
        const int dsId = slash > 0 ? body.left(slash).toInt() : 0;
        const QString table = slash > 0 ? body.mid(slash + 1) : QString();

        auto *panel = new nextpilot::plotscope::data::DataTablePanel(m_app, m_executor, parent);
        panel->openDataset(dsId, table);
        m_open.insert(ref.uri, QPointer<QWidget>(panel));
        QObject::connect(panel, &QObject::destroyed, panel, [this, uri = ref.uri]() { m_open.remove(uri); });
        return panel;
    }

private:
    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    QHash<QString, QPointer<QWidget>> m_open; // uri → 已建页签（同一 ref 复用）
};

// 借用指针的所有权（ViewRegistry 只借用不 delete；进程存续期持有）。
std::unique_ptr<DataEditorProvider> &editorProvider()
{
    static std::unique_ptr<DataEditorProvider> s_provider;
    return s_provider;
}

} // namespace

bool installDataViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                              intent::IIntentExecutor *executor)
{
    if (!services || !appCtx || !executor)
        return false;
    auto *views = services->getService<nextpilot::plotscope::registry::ViewRegistry>(QStringLiteral("views.registry"));
    if (!views)
        return false;

    const QString owner = QStringLiteral("data");

    // ---- 声明半（owner "data"）：views + customEditors，先于 provider 登记 ----
    // registerEditorProvider 要求 viewType 已声明且 owner 一致（2a 防冒名线），
    // 模块 initialize 晚于本调用（host.activate 在 build 内才跑），声明在此落。
    // 声明代码物理在本 TU（modules/data/gui，data 模块目录）——data 自声明，
    // 壳不代声明（§7 打回线 1）；壳只负责在本函数被调用时提供 services。
    {
        nextpilot::plotscope::registry::ContributesDoc doc;
        doc.owner = owner;

        nextpilot::plotscope::registry::ContributedViewContainer sidebar;
        sidebar.id = QStringLiteral("data.sidebar");
        sidebar.title = QStringLiteral("数据源");
        sidebar.area = QStringLiteral("sidebar"); // 壳落区映射：sidebar → PrimarySidebar
        sidebar.order = 0;
        doc.viewContainers.append(sidebar);

        nextpilot::plotscope::registry::ContributedView view;
        view.container = QStringLiteral("data.sidebar");
        view.id = QStringLiteral("data.datasetTree");
        view.name = QStringLiteral("数据源");
        view.icon = QStringLiteral("datasource");
        view.order = 0;
        doc.views.append(view);

        nextpilot::plotscope::registry::ContributedCustomEditor editor;
        editor.viewType = QStringLiteral("data.table");
        editor.displayName = QStringLiteral("数据表格");
        editor.priority = 0;
        doc.customEditors.append(editor);

        views->declare(doc);
    }

    // ---- 注册半 ----
    bool ok = true;
    // data.datasetTree（v3 表 A：sidebar；声明 owner "data"）。
    ok &= views->registerViewProvider(QStringLiteral("data.datasetTree"),
                                      &nextpilot::plotscope::gui::createDatasetTreePanel, owner);
    // data.table（editor；声明已在本函数落）。
    if (!editorProvider())
        editorProvider().reset(new DataEditorProvider(*appCtx, *executor));
    ok &= views->registerEditorProvider(QStringLiteral("data.table"), editorProvider().get(), owner) != nullptr;
    return ok;
}

} // namespace nextpilot::plotscope::data
