#include "WorkbenchServices.h"

namespace nextpilot::plotscope::assembly {

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
      m_panelsService(appCtx),
      m_formatHints(appCtx),
      m_themeService(appCtx, hostRef),
      m_slotsService(hostRef.uiSlotsMutable()),
      m_viewsService(m_viewRegistry),
      m_contribInstaller(appCtx, m_slotsService, m_viewRegistry)
{
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
}

} // namespace nextpilot::plotscope::assembly
