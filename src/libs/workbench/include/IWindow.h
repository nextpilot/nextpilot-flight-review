#pragma once

// vscode.window 对位：窗口/交互能力接缝。服务 id "vscode.window"。
// headless 用 NullWindow；GUI 真实现归 A2b。状态栏不在本接口（已有 StatusHub）。

#include "VsCodeTypes.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// state() 返回类型只前向声明；真正的 PersistentStateHub 由装配层注入。
namespace nextpilot::plotscope::context
{
class PersistentStateHub;
}

namespace nextpilot::plotscope::workbench
{

class IWindow
{
public:
    virtual ~IWindow() = default;

    // nameFilters：QFileDialog 的过滤器串（同 GuiContext.cpp:237），如 "CSV (*.csv);;文本 (*.txt)"。
    virtual std::optional<QString> showOpenDialog(const QString &title,
                                                  const QString &nameFilters = {},
                                                  const QString &dir = {}) = 0;
    virtual std::optional<QString> showSaveDialog(const QString &title,
                                                  const QString &nameFilters = {},
                                                  const QString &suggested = {}) = 0;
    virtual void showInformationMessage(const QString &message) = 0;
    virtual void showWarningMessage(const QString &message) = 0;
    virtual void showErrorMessage(const QString &message) = 0;
    virtual void withProgress(const QString &title,
                              const std::function<void(int, const QString &)> &report) = 0;
    // 不设 createOutputChannel：仓库无 IOutputChannel，输出留 A2b DebugConsole / A8b window.outputs。
    virtual std::optional<int> showQuickPick(const QStringList &items,
                                             const QString &title) = 0;
    virtual std::optional<QString> showInputBox(const QString &title,
                                                const QString &valueHint = {}) = 0;

    // 承载 workspace 的"当前焦点"（类比 window.activeTextEditor / showTextDocument）。
    virtual int activeSource() const = 0;
    virtual void showSource(int sourceId) = 0;
    virtual void setActiveChannel(int datasetId, const QString &table, const QString &column) = 0;

    // 会话 UI 状态（libs 具体类型，装配层注入）。
    virtual nextpilot::plotscope::context::PersistentStateHub &state() = 0;
};

} // namespace nextpilot::plotscope::workbench
