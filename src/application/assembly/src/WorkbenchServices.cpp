#include "WorkbenchServices.h"

namespace nextpilot::plotscope::assembly
{

WorkbenchServices::WorkbenchServices(nextpilot::plotscope::context::AppContext &appCtx,
                                     nextpilot::plotscope::intent::JobRecorder &recorder,
                                     host::RuntimeHost &hostRef, QObject *parent)
    : QObject(parent),
      m_datasetAccess(appCtx.data(), this),
      m_sourceAccess(appCtx.sources(), this),
      m_ingestAccess(appCtx, this),
      m_recorderAccess(recorder),
      m_playbackAccess(),
      m_workspace(m_datasetAccess, m_sourceAccess, m_ingestAccess,
                  m_recorderAccess, m_playbackAccess,
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
      m_scriptCaps(appCtx.interactions(), appCtx, hostRef, &recorder,
                   [&hostRef]() { return hostRef.registeredOps(); })
{
    // §1.4.2 + §1.2.1：注册 4 条服务。owner=assembly，version=1。
    m_services.registerSingleton(QStringLiteral("vscode.workspace"), &m_workspace,
                                 QStringLiteral("assembly"), 1, nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IWorkspace>::value());
    m_services.registerSingleton(QStringLiteral("vscode.window"), &m_window,
                                 QStringLiteral("assembly"), 1, nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::IWindow>::value());
    // ICommands 是 CommandsAdapter 的第二基类（QObject 在前）。registerSingleton 存 void*、
    // getService<T> 用 static_cast<T*>(void*) 取回——该转换不做基类偏移调整，
    // 因此这里必须存【接口子对象地址】而不是派生对象起始地址，否则取回的 ICommands*
    // 实际指向 QObject 子对象，虚调用全乱。其余服务都是单继承首基类，无此问题。
    auto *commandsIface =
        static_cast<nextpilot::plotscope::workbench::ICommands *>(&m_commands);
    m_services.registerSingleton(QStringLiteral("vscode.commands"), commandsIface,
                                 QStringLiteral("assembly"), 1, nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::workbench::ICommands>::value());
    m_services.registerSingleton(QStringLiteral("script.capabilities"), &m_scriptCaps,
                                 QStringLiteral("assembly"), 1, nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::uischema::IScriptCapabilities>::value());
}

} // namespace nextpilot::plotscope::assembly
