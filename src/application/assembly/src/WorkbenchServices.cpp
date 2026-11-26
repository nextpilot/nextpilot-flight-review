#include "WorkbenchServices.h"

namespace nextpilot::plotscope::assembly {

// ---- WorkbenchCommandHandler ----
WorkbenchCommandHandler::WorkbenchCommandHandler(wb::IEditorService &editors,
                                                 nextpilot::plotscope::context::AppContext &appCtx,
                                                 intent::IIntentExecutor &executor, QObject *parent)
    : QObject(parent), m_editors(editors), m_appCtx(appCtx), m_executor(executor)
{
}

bool WorkbenchCommandHandler::canHandle(const intent::Intent &intent) const
{
    // §3.0.9 内置命令最小集：本笔只交付 openSettings；其余 workbench.* 待后续笔逐个接入。
    return intent.op == QLatin1String("workbench.action.openSettings");
}

foundation::Result<intent::IntentResult> WorkbenchCommandHandler::execute(const intent::Intent &intent)
{
    // 执行体 = 打开内置 settings 编辑器（普通 editor 页签，无模态对话框）。
    registry::ResourceRef ref{QStringLiteral("settings:"), QStringLiteral("application/json"), QStringLiteral("设置")};
    QWidget *page = m_editors.openEditor(BuiltinSettingsEditorProvider::viewType(), ref, m_appCtx, m_executor, nullptr);
    if (page)
        m_lastOpened = page; // 页签能回答自己的 ResourceRef（e2e 断言）
    intent::IntentResult out;
    out.payload = intent.op;
    return foundation::Result<intent::IntentResult>(out);
}

WorkbenchServices::WorkbenchServices(nextpilot::plotscope::context::AppContext &appCtx,
                                     nextpilot::plotscope::intent::JobRecorder &recorder, host::RuntimeHost &hostRef,
                                     QObject *parent)
    : QObject(parent), m_datasetAccess(appCtx.data(), this), m_sourceAccess(appCtx.sources(), this),
      m_ingestAccess(appCtx, this), m_recorderAccess(recorder), m_playbackAccess(),
      m_workspace(m_datasetAccess, m_sourceAccess, m_ingestAccess, m_recorderAccess, m_playbackAccess,
                  appCtx.settings(), appCtx.derivedHub(),
                  [&appCtx]() -> QVector<QPair<QString, QString>> {
                      QVector<QPair<QString, QString>> out;
                      for (auto *d : appCtx.toolbox().drivers())
                          if (d)
                              out.append({d->id(), d->displayName()});
                      return out;
                  }),
      m_commands(
          [&hostRef](const QString &id, nextpilot::plotscope::intent::IIntentHandler *h) {
              // commandId == Intent.op：用保留前缀注入 RuntimeHost 系统级池，
              // 保证执行链仍只有 RuntimeHost::execute 一条（旧 GUI/脚本路径不退化）。
              hostRef.registerSystemHandler(QStringLiteral("cmd/") + id, h);
          },
          this),
      m_scriptCaps(appCtx.interactions(), appCtx, hostRef, &recorder, [&hostRef]() { return hostRef.registeredOps(); }),
      m_panelsService(appCtx), m_formatHints(appCtx), m_themeService(appCtx, hostRef),
      m_slotsService(hostRef.uiSlotsMutable()), m_viewsService(m_viewRegistry), m_editorsService(m_viewRegistry),
      m_configurationService(appCtx.settings(), appCtx.toolbox()), m_settingsEditor(m_configurationService),
      m_contribInstaller(appCtx, m_slotsService, m_viewRegistry)
{
    // 壳内置 text 最小编辑器：装配层声明 viewType="text"
    // 并注册 provider——零扩展也能打开/编辑/保存。owner="core.text" 是装配层自数据，
    // 不是"壳按 owner 分支"（壳不见 registry，也不解释业务 id）。
    {
        registry::ContributesDoc coreDoc;
        coreDoc.owner = QStringLiteral("core.text");
        registry::ContributedCustomEditor text;
        text.viewType = QStringLiteral("text");
        text.displayName = QStringLiteral("纯文本");
        text.priority = -1; // 扩展编辑器命中时优先于内置
        registry::ContributedEditorSelector sel;
        sel.mime = QStringLiteral("text/plain");
        text.selector.append(sel);
        coreDoc.customEditors.append(text);
        m_viewRegistry.declare(coreDoc);
        m_textEditorDisposable = m_viewRegistry.registerEditorProvider(
            QStringLiteral("text"), &m_textEditor, QStringLiteral("core.text"));
    }
    // 壳内置 Settings editor：owner=core.settings 经 registerEditorProvider 的
    // registerEditorProvider 注册（无第二注册面、壳不按 id 特判）。viewType 核过
    // VSCode 源码：SettingsEditor2.ID = 'workbench.editor.settings2'（
    // 'workbench.editor.settings' 字符串在源码中不存在，实际 ID 带 "2"）。
    // core.settings 自带「通用」配置组进 configurations()（零 owner 也能开设置，§5）。
    {
        registry::ContributesDoc settingsDoc;
        settingsDoc.owner = QStringLiteral("core.settings");
        registry::ContributedCustomEditor settingsDecl;
        settingsDecl.viewType = BuiltinSettingsEditorProvider::viewType();
        settingsDecl.displayName = QStringLiteral("设置");
        settingsDecl.priority = -10; // 扩展编辑器命中时优先于内置
        registry::ContributedEditorSelector jsonSel;
        jsonSel.mime = QStringLiteral("application/json");
        settingsDecl.selector.append(jsonSel);
        settingsDoc.customEditors.append(settingsDecl);
        m_viewRegistry.declare(settingsDoc);
        m_settingsEditorDisposable = m_viewRegistry.registerEditorProvider(
            BuiltinSettingsEditorProvider::viewType(), &m_settingsEditor, QStringLiteral("core.settings"));
    }
    // core.settings 的命令与「通用」配置走唯一贡献表：命令面板可见、
    // --contributions 可列（判据 grep）；配置声明进 configurations()（§5 零 owner 可开）。
    {
        registry::ContributesDoc coreCfg;
        coreCfg.owner = QStringLiteral("core.settings");
        registry::ContributedCommand openSettings;
        openSettings.command = QStringLiteral("workbench.action.openSettings");
        openSettings.title = QStringLiteral("打开设置");
        openSettings.category = QStringLiteral("首选项");
        openSettings.order = 0;
        coreCfg.commands.append(openSettings);
        coreCfg.configurations = coreBuiltinConfigurations();
        appCtx.toolbox().mountContribution(coreCfg);
        // 执行体：工作台内置命令 handler（workbench.* 保留前缀独占，扩展 handler 让出）。
        // 同时注册成服务（workbench.commands）：e2e 可断言"命令执行后页签能回答 ResourceRef"。
        m_workbenchHandler = new WorkbenchCommandHandler(m_editorsService, appCtx, hostRef, this);
        hostRef.registerSystemHandler(QStringLiteral("@system/workbench"), m_workbenchHandler);
        m_services.registerSingleton(QStringLiteral("workbench.commands"), m_workbenchHandler,
                                     QStringLiteral("assembly"), 1,
                                     nextpilot::plotscope::instantiation::ServiceTypeTag<
                                         nextpilot::plotscope::assembly::WorkbenchCommandHandler>::value());
    }
    // §1.4.2 + §1.2.1：注册 4 条服务。owner=assembly，version=1。
    m_services.registerSingleton(
        QStringLiteral("vscode.workspace"), &m_workspace, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IWorkspace>::value());
    m_services.registerSingleton(
        QStringLiteral("vscode.window"), &m_window, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IWindow>::value());
    // ICommands 是 CommandsAdapter 的第二基类（QObject 在前）。registerSingleton 存 void*、
    // getService<T> 用 static_cast<T*>(void*) 取回——该转换不做基类偏移调整，
    // 因此这里必须存【接口子对象地址】而不是派生对象起始地址，否则取回的 ICommands*
    // 实际指向 QObject 子对象，虚调用全乱。其余服务都是单继承首基类，无此问题。
    auto *commandsIface = static_cast<nextpilot::plotscope::workbench::ICommands *>(&m_commands);
    m_services.registerSingleton(
        QStringLiteral("vscode.commands"), commandsIface, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::ICommands>::value());
    m_services.registerSingleton(QStringLiteral("script.capabilities"), &m_scriptCaps, QStringLiteral("assembly"), 1,
                                 nextpilot::plotscope::instantiation::ServiceTypeTag<
                                     nextpilot::plotscope::uischema::IScriptCapabilities>::value());
    // 壳读回三服务（裁定 52441633 §三）：MainWindow::build 用同 id + 同 typeTag 取回。
    m_services.registerSingleton(
        QStringLiteral("panels.readback"), &m_panelsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::workbench::IPanelsService>::value());
    m_services.registerSingleton(
        QStringLiteral("format.hints"), &m_formatHints, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::workbench::IFormatHints>::value());
    m_services.registerSingleton(
        QStringLiteral("theme.readback"), &m_themeService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::workbench::IThemeService>::value());
    // 槽位表服务（裁定 52442006 Step 2）：壳读写唯一通道。
    m_services.registerSingleton(
        QStringLiteral("slots.binding"), &m_slotsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::workbench::ISlotsService>::value());
    // 视图注册表（Step 5）：写面给装配层内部（ExtensionActivationService 经
    // getService<ViewRegistry> 灌数），读/打开面给壳（IViewsService）。
    m_services.registerSingleton(
        QStringLiteral("views.registry"), &m_viewRegistry, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::registry::ViewRegistry>::value());
    m_services.registerSingleton(
        QStringLiteral("views.readback"), &m_viewsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::workbench::IViewsService>::value());
    // 编辑器读回：写面复用 views.registry（ViewRegistry），读/打开面给壳。
    m_services.registerSingleton(
        QStringLiteral("editor.readback"), &m_editorsService, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<
            nextpilot::plotscope::workbench::IEditorService>::value());
    // 配置合并/复位/诊断：声明面读 ToolboxHub，持久化只落 SettingsStore。
    m_services.registerSingleton(QStringLiteral("configuration.readback"), &m_configurationService,
                                 QStringLiteral("assembly"), 1,
                                 nextpilot::plotscope::instantiation::ServiceTypeTag<
                                     nextpilot::plotscope::workbench::IConfigurationService>::value());
}

} // namespace nextpilot::plotscope::assembly
