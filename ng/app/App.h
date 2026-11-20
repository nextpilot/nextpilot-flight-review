#pragma once

#include "AppContext.h"
#include "JobRecorder.h"
#include "RuntimeHost.h"

#include <QObject>

namespace nextpilot::plotscope::app
{

// 组合根：**app 的装配点**。
//
// 它做三件事，且只做这三件：
//  1. 构造 AppContext（进程级运行态的唯一持有者）；
//  2. 把已自注册的器官工厂与知识包装进宿主；
//  3. 驱动启动（headless / GUI 两种模式）。
//
// 它**不持有**业务状态——数据集、设置、工具箱都在 AppContext 里。
// 这样"谁拥有状态"只有一个答案，避免状态在 App / RuntimeHost 两处分叉。
//
// GUI 与 CLI 共用这一个类；区别只在是否传入 UiContext。
class App : public QObject
{
    Q_OBJECT
public:
    // settingsPath 为空时设置存内存（测试/headless）；否则读写该 JSON 文件。
    explicit App(const QString &settingsPath = QString(), QObject *parent = nullptr);

    // 进程级运行态。
    nextpilot::plotscope::context::AppContext &context() { return m_ctx; }
    const nextpilot::plotscope::context::AppContext &context() const { return m_ctx; }

    // 便捷转发（内部使用）。
    nextpilot::plotscope::dataset::DatasetRegistry &data() { return m_ctx.data(); }
    nextpilot::plotscope::host::RuntimeHost &host() { return m_host; }
    nextpilot::plotscope::intent::JobRecorder &recorder() { return m_recorder; }

    // headless=true 时不建 UI 上下文，模块的 onAttach 不被调用。
    QHash<QString, QString> startup(bool headless,
                                    const QStringList &disabled = {});

private:
    nextpilot::plotscope::context::AppContext m_ctx;
    nextpilot::plotscope::host::RuntimeHost m_host;
    nextpilot::plotscope::intent::JobRecorder m_recorder;
};

} // namespace nextpilot::plotscope::app
