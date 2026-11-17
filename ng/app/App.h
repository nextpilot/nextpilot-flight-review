#pragma once

#include "ModuleHost.h"
#include "DataRepository.h"

#include <QObject>

namespace ng::app
{

// 组合根：注册所有模块工厂并驱动装载。
// GUI 与 CLI 共用这一个类；区别只在是否传入 UiContext。
class App : public QObject
{
    Q_OBJECT
public:
    explicit App(QObject *parent = nullptr);

    ng::core::DataRepository &data() { return m_repo; }
    ng::host::ModuleHost &modules() { return m_host; }

    // headless=true 时不建 UI 上下文，模块的 onAttach 不被调用。
    QHash<QString, QString> startup(bool headless,
                                    const QStringList &disabled = {});

private:
    ng::core::DataRepository m_repo;
    ng::host::ModuleHost m_host;
};

} // namespace ng::app
