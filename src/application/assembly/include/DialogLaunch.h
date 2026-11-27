#pragma once

// 对话框按 id 触发的装配层助手（A2d 第⑤步）：壳（MainWindow / gui_main）零
// libs/registry 引用——组合根经本助手落到 DialogLauncherHub 注册的启动器
// （对话框实现在模块呈现层，D18a）。未注册 no-op（裸壳/headless 安全）。

#include "DialogLauncherHub.h"

#include <QString>

class QWidget;

namespace nextpilot::plotscope::context {
class AppContext;
}
namespace nextpilot::plotscope::intent {
class IIntentExecutor;
}

namespace nextpilot::plotscope::assembly {

inline void launchDialogById(const QString &actionId, QWidget *parent,
                             nextpilot::plotscope::context::AppContext &appCtx,
                             nextpilot::plotscope::intent::IIntentExecutor &executor)
{
    nextpilot::plotscope::registry::DialogLauncherHub::launch(actionId, parent, appCtx, executor);
}

} // namespace nextpilot::plotscope::assembly
