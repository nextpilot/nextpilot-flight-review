#pragma once

#include "Intent.h"

#include <QObject>

namespace nextpilot::plotscope::context
{
class AppContext;
}
namespace nextpilot::plotscope::host
{
class RuntimeHost;
}

namespace nextpilot::plotscope::gui
{

// `preferences.open` —— 打开全局偏好设置（「文件 → 设置」/ Ctrl+, / app.call）。
//
// 为什么放在 GUI 外壳、而不是 modules/settings：
//   modules/settings 的边界是"纯存取机制，不知道任何具体设置项、不碰 QWidget"。
//   "打开设置界面"是 GUI 专属意图，与 theme.set / theme.toggle 同一性质——
//   由 GUI 注册的**系统级 handler** 处理（registerSystemHandler）；
//   headless 里没有这个 handler，调用方得到诚实的"没有模块能处理意图"。
//
// op 前缀必须是 `preferences.`（不能用 `settings.`）：SettingsModule 的
// canHandle 是 startsWith("settings.")，用那个前缀会被它接走并判为未知操作。
//
// 生命周期随 MainWindow：模态对话框在 execute 里现构造现销毁。
class PreferencesIntentHandler final : public QObject, public intent::IIntentHandler
{
    Q_OBJECT
public:
    // appCtx：设置存储（对话框只读展示落盘路径）+ view action 派发（时间轴换刻度）。
    // host：作为 IIntentExecutor 供对话框读写设置（settings.get/set/flush）。
    PreferencesIntentHandler(context::AppContext &appCtx, host::RuntimeHost &host,
                             QObject *parent = nullptr);

    bool canHandle(const intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<intent::IntentResult>
    execute(const intent::Intent &intent) override;

private:
    void show(const QString &section);

    context::AppContext &m_appCtx;
    host::RuntimeHost &m_host;
};

} // namespace nextpilot::plotscope::gui
