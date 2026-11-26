#pragma once

// A1b §1.4 装配层组合：持有 InstantiationService，构造各 adapter，注册 3 条 VSCode 服务。
// 组合根（App）拥有本对象；RuntimeHost 通过引用拿到 InstantiationService 传给模块。
//
// 只注册 3 条：vscode.workspace / vscode.window / vscode.commands。
// 5 个 access 是 IWorkspace 成员，不单独注册（§1.3）。

#include "InstantiationService.h"
#include "WorkbenchAdapters.h"
#include "ExtensionActivationService.h" // ContributionsInstaller（裁定 52442006 Step 3）
#include "IDisposable.h"
#include "Intent.h"
#include "NullWindow.h"
#include "RuntimeHost.h"

#include <QPointer>
#include <memory>

namespace nextpilot::plotscope::assembly {
namespace inst = nextpilot::plotscope::instantiation;
namespace wb = nextpilot::plotscope::workbench;
namespace intent = nextpilot::plotscope::intent;
namespace host = nextpilot::plotscope::host;

// 工作台内置命令执行体：认领 workbench.* 保留前缀的内置命令
// （§3.0.9 内置命令最小集，本笔交付 openSettings）。扩展 handler 让出该前缀，
// claim 唯一、与 QHash 遍历序无关。
class WorkbenchCommandHandler final : public QObject, public intent::IIntentHandler
{
public:
    WorkbenchCommandHandler(wb::IEditorService &editors, nextpilot::plotscope::context::AppContext &appCtx,
                            intent::IIntentExecutor &executor, QObject *parent = nullptr);

    bool canHandle(const intent::Intent &intent) const override;
    foundation::Result<intent::IntentResult> execute(const intent::Intent &intent) override;

    // 命令执行后创建的编辑器页（e2e 断言"页签能回答 ResourceRef"用）。
    QWidget *lastOpenedEditor() const { return m_lastOpened.data(); }

private:
    wb::IEditorService &m_editors;
    nextpilot::plotscope::context::AppContext &m_appCtx;
    intent::IIntentExecutor &m_executor;
    QPointer<QWidget> m_lastOpened;
};

// 一次性把整套服务装配好：构造 adapter → registerSingleton → 暴露 services 给宿主。
class WorkbenchServices : public QObject
{
    Q_OBJECT
public:
    WorkbenchServices(nextpilot::plotscope::context::AppContext &appCtx,
                      nextpilot::plotscope::intent::JobRecorder &recorder, host::RuntimeHost &hostRef,
                      QObject *parent = nullptr);

    inst::InstantiationService &services() { return m_services; }

    // 模块声明灌回入口（裁定 52442006 Step 3）：App 组合根接线到 RuntimeHost，
    // host.activate 收集 contributes 后调它——壳不再灌回。
    wb::IContributionsInstaller *contributionsInstaller() { return &m_contribInstaller; }

private:
    inst::InstantiationService m_services;

    DatasetAccessAdapter m_datasetAccess;
    SourceAccessAdapter m_sourceAccess;
    IngestAdapter m_ingestAccess;
    RecorderAdapter m_recorderAccess;
    PlaybackAdapter m_playbackAccess;
    WorkspaceAdapter m_workspace;
    CommandsAdapter m_commands;
    ScriptCapabilitiesAdapter m_scriptCaps;
    // 壳读回三服务（A8b-M2 裁定 52441633 §三）：壳经 workbench 接口读回，
    // src/application/gui 不再出现 ToolboxHub / toolbox()。
    PanelsServiceAdapter m_panelsService;
    FormatHintsAdapter m_formatHints;
    ThemeServiceAdapter m_themeService;
    // 槽位表服务（裁定 52442006 Step 1/2）：壳读写唯一通道；灌回经 m_contribInstaller。
    SlotsServiceAdapter m_slotsService;
    // 视图注册表（Step 5）：模块 contributes.views 与扩展包 manifest 汇同一张表，
    // 壳经 IViewsService 读回/打开。
    registry::ViewRegistry m_viewRegistry;
    ViewsServiceAdapter m_viewsService;
    // 编辑器读回：壳经 IEditorService 读回/打开，不见 registry::。
    EditorServiceAdapter m_editorsService;
    // 配置合并/复位/诊断：壳经 IConfigurationService 读写设置，
    // 持久化只落 SettingsStore（app 唯一出口）。
    ConfigurationServiceAdapter m_configurationService;
    // 壳内置 Settings editor（零扩展也能开设置）。
    BuiltinSettingsEditorProvider m_settingsEditor;
    std::unique_ptr<registry::IDisposable> m_settingsEditorDisposable; // 与成员同生命周期
    // 工作台内置命令执行体：workbench.action.openSettings。
    WorkbenchCommandHandler *m_workbenchHandler = nullptr; // 父归 WorkbenchServices（QObject）
    // 壳内置 text 最小编辑器（零扩展也能打开/编辑/保存）。
    BuiltinTextEditorProvider m_textEditor;
    std::unique_ptr<registry::IDisposable> m_textEditorDisposable; // 与成员同生命周期，析构先于 registry
    ContributionsInstaller m_contribInstaller;
    wb::NullWindow m_window;
};

} // namespace nextpilot::plotscope::assembly
