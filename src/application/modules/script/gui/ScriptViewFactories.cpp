#include "ScriptViewFactories.h"

#include "ContributionParser.h"
#include "ScriptPanels.h"
#include "BatchPanel.h"
#include "IIntentExecutor.h"
#include "InstantiationService.h"
#include "ViewRegistry.h"

#include "AppContext.h"

#include <QHash>
#include <QPointer>

#include <memory>

// script 视图装配·注册半（A2c）：声明在此落（owner "script"），provider 登记也在此。
// 不写 using namespace dataset|parser（A2c 判据：gui 面 0）。

namespace nextpilot::plotscope::script {

namespace {

// ---- script.editor provider ----
//
// ResourceRef 构成（A2c PR 必答：viewType → scheme → uri 表；uri 语义归 owner）：
//   scheme  : script://
//   uri     : script://editor（编辑器单实例：多文档在面板内页签管理，同 ref 复用）
//   mime    : application/x-script-editor
//   title   : 脚本编辑器
class EditorProvider final : public nextpilot::plotscope::registry::IEditorProvider
{
public:
    EditorProvider(nextpilot::plotscope::context::AppContext &appCtx,
                   nextpilot::plotscope::intent::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor)
    {
    }

    bool canOpen(const nextpilot::plotscope::registry::ResourceRef &ref) const override
    {
        return ref.uri == QLatin1String("script://editor");
    }

    QWidget *createEditor(const nextpilot::plotscope::registry::ResourceRef &ref, QWidget *parent) override
    {
        Q_UNUSED(ref);
        auto it = m_open.find(ref.uri);
        if (it != m_open.end() && !it.value().isNull())
            return it.value().data();
        QWidget *panel = createScriptEditorPanel(m_app, m_executor, parent);
        m_open.insert(ref.uri, QPointer<QWidget>(panel));
        QObject::connect(panel, &QObject::destroyed, panel, [this, uri = ref.uri]() { m_open.remove(uri); });
        return panel;
    }

private:
    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    QHash<QString, QPointer<QWidget>> m_open; // uri → 已建页签（同一 ref 复用）
};

// ---- script.batchSummary provider ----
//
// ResourceRef 构成：
//   scheme  : script://
//   uri     : script://batch/summary（汇总面板单实例，同 ref 复用 = 聚焦语义）
//   mime    : application/x-script-batch-summary
//   title   : 批量汇总
class BatchSummaryProvider final : public nextpilot::plotscope::registry::IEditorProvider
{
public:
    BatchSummaryProvider(nextpilot::plotscope::context::AppContext &appCtx,
                         nextpilot::plotscope::intent::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor)
    {
    }

    bool canOpen(const nextpilot::plotscope::registry::ResourceRef &ref) const override
    {
        return ref.uri == QLatin1String("script://batch/summary");
    }

    QWidget *createEditor(const nextpilot::plotscope::registry::ResourceRef &ref, QWidget *parent) override
    {
        Q_UNUSED(ref);
        auto it = m_open.find(ref.uri);
        if (it != m_open.end() && !it.value().isNull())
            return it.value().data();
        QWidget *panel = createBatchSummaryPanel(m_app, m_executor, parent);
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
std::unique_ptr<EditorProvider> &editorProvider()
{
    static std::unique_ptr<EditorProvider> s_provider;
    return s_provider;
}
std::unique_ptr<BatchSummaryProvider> &batchProvider()
{
    static std::unique_ptr<BatchSummaryProvider> s_provider;
    return s_provider;
}

} // namespace

bool installScriptViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                                intent::IIntentExecutor *executor)
{
    if (!services || !appCtx || !executor)
        return false;
    auto *views = services->getService<nextpilot::plotscope::registry::ViewRegistry>(QStringLiteral("views.registry"));
    if (!views)
        return false;

    const QString owner = QStringLiteral("script");

    // ---- 声明半（owner "script"）：views + customEditors，先于 provider 登记 ----
    // registerEditorProvider 要求 viewType 已声明且 owner 一致（2a 防冒名线），
    // 模块 initialize 晚于本调用（host.activate 在 build 内才跑），声明在此落。
    // 声明代码物理在本 TU（modules/script/gui，script 模块目录）——script 自声明，
    // 壳不代声明（A2c 打回线 1）；壳只负责在本函数被调用时提供 services。
    {
        nextpilot::plotscope::registry::ContributesDoc doc;
        doc.owner = owner;

        nextpilot::plotscope::registry::ContributedViewContainer sidebar;
        sidebar.id = QStringLiteral("script.sidebar");
        sidebar.title = QStringLiteral("脚本");
        sidebar.area = QStringLiteral("sidebar"); // 壳落区映射：sidebar → PrimarySidebar
        sidebar.order = 2;
        doc.viewContainers.append(sidebar);

        nextpilot::plotscope::registry::ContributedViewContainer bottom;
        bottom.id = QStringLiteral("script.bottom");
        bottom.title = QStringLiteral("终端");
        bottom.area = QStringLiteral("bottom"); // → 底部 Panel
        bottom.order = 0;
        doc.viewContainers.append(bottom);

        nextpilot::plotscope::registry::ContributedViewContainer auxiliary;
        auxiliary.id = QStringLiteral("script.auxiliary");
        auxiliary.title = QStringLiteral("调试");
        auxiliary.area = QStringLiteral("auxiliary"); // → SecondarySidebar
        auxiliary.order = 0;
        doc.viewContainers.append(auxiliary);

        // script-files → script.files（左栏 sidebar）。
        nextpilot::plotscope::registry::ContributedView files;
        files.container = QStringLiteral("script.sidebar");
        files.id = QStringLiteral("script.files");
        files.name = QStringLiteral("脚本文件");
        files.icon = QStringLiteral("script");
        files.order = 0;
        doc.views.append(files);

        // script-console → script.console（底部 bottom，VS Code「终端」语义）。
        nextpilot::plotscope::registry::ContributedView console;
        console.container = QStringLiteral("script.bottom");
        console.id = QStringLiteral("script.console");
        console.name = QStringLiteral("终端");
        console.icon = QStringLiteral("terminal");
        console.order = 0;
        doc.views.append(console);

        // script-debug → script.debug（右栏 auxiliary）。
        nextpilot::plotscope::registry::ContributedView debug;
        debug.container = QStringLiteral("script.auxiliary");
        debug.id = QStringLiteral("script.debug");
        debug.name = QStringLiteral("调试");
        debug.icon = QStringLiteral("script_debug");
        debug.order = 0;
        doc.views.append(debug);

        nextpilot::plotscope::registry::ContributedCustomEditor editor;
        editor.viewType = QStringLiteral("script.editor");
        editor.displayName = QStringLiteral("编辑器");
        editor.priority = 0;
        doc.customEditors.append(editor);

        nextpilot::plotscope::registry::ContributedCustomEditor batch;
        batch.viewType = QStringLiteral("script.batchSummary");
        batch.displayName = QStringLiteral("批量汇总");
        batch.priority = 0;
        doc.customEditors.append(batch);

        views->declare(doc);
    }

    // ---- 注册半 ----
    bool ok = true;
    ok &= views->registerViewProvider(QStringLiteral("script.files"), &createScriptFilesPanel, owner);
    ok &= views->registerViewProvider(QStringLiteral("script.console"), &createScriptConsolePanel, owner);
    ok &= views->registerViewProvider(QStringLiteral("script.debug"), &createScriptDebugPanel, owner);
    if (!editorProvider())
        editorProvider().reset(new EditorProvider(*appCtx, *executor));
    ok &= views->registerEditorProvider(QStringLiteral("script.editor"), editorProvider().get(), owner) != nullptr;
    if (!batchProvider())
        batchProvider().reset(new BatchSummaryProvider(*appCtx, *executor));
    ok &= views->registerEditorProvider(QStringLiteral("script.batchSummary"), batchProvider().get(), owner) != nullptr;
    return ok;
}

} // namespace nextpilot::plotscope::script
