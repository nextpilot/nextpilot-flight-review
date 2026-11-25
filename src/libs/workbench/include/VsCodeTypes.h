#pragma once

// VSCode 服务接口共用的值类型（对标 vscode 命名空间里的结构）。
// 只依赖 Qt Core + foundation::Result + STL，headless 可用。

#include "Result.h"

#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QStringList>

#include <functional>

namespace nextpilot::plotscope::workbench
{

// 仓库统一错误结果（对齐 InteractionHub::ViewActionHandler 的签名口径）。
using nextpilot::plotscope::foundation::Result;

// vscode.commands 的 Handler 别名定义在 ICommands 类内（见 ICommands.h，
// 正文 §1.3 要求 ICommands::Handler）。
//
// 注意：不定义 FileFilter / IOutputChannel。施工卡第一版自造的这两个类型在仓库无对应物，
// 已按 §1.3.2 类型对齐表删除：文件对话框过滤器用 QFileDialog 的 nameFilters 字符串
// （同 GuiContext.cpp:237），输出通道本项不做（留 A2b DebugConsole / A8b window.outputs）。

} // namespace nextpilot::plotscope::workbench
