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
#include "NullWindow.h"
#include "RuntimeHost.h"

#include <memory>

namespace nextpilot::plotscope::assembly {
namespace inst = nextpilot::plotscope::instantiation;
namespace wb = nextpilot::plotscope::workbench;
namespace host = nextpilot::plotscope::host;

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
    // 编辑器读回（M3-a 第 2 笔）：壳经 IEditorService 读回/打开，不见 registry::。
    EditorServiceAdapter m_editorsService;
    // 壳内置 text 最小编辑器（零扩展也能打开/编辑/保存）。
    BuiltinTextEditorProvider m_textEditor;
    std::unique_ptr<registry::IDisposable> m_textEditorDisposable; // 与成员同生命周期，析构先于 registry
    ContributionsInstaller m_contribInstaller;
    wb::NullWindow m_window;
};

} // namespace nextpilot::plotscope::assembly
