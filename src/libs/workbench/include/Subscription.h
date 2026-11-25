#pragma once

// VSCode Event 模式的可撤销句柄（对标 vscode.Disposable）。
//
// 数据变更事件（onDidAddDocument 等）返回一个 Subscription；调用方用
// QObject::disconnect(subscription) 退订。仓库无自带 Disposable 类型，
// 按 §1.3.2 类型对齐表用 Qt 的 QMetaObject::Connection（Qt Core 类型，非业务类型）。

#include <QMetaObject> // QMetaObject::Connection

namespace nextpilot::plotscope::workbench {

using Subscription = QMetaObject::Connection;

} // namespace nextpilot::plotscope::workbench
