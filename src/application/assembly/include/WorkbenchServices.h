#pragma once

// A1b §1.4 装配层组合：持有 InstantiationService，构造各 adapter，注册 3 条 VSCode 服务。
// 组合根（App）拥有本对象；RuntimeHost 通过引用拿到 InstantiationService 传给模块。
//
// 只注册 3 条：vscode.workspace / vscode.window / vscode.commands。
// 5 个 access 是 IWorkspace 成员，不单独注册（§1.3）。

#include "InstantiationService.h"
#include "WorkbenchAdapters.h"
#include "NullWindow.h"
#include "RuntimeHost.h"

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
    wb::NullWindow m_window;
};

} // namespace nextpilot::plotscope::assembly
