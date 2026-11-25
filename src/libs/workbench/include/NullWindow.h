#pragma once

// IWindow 的 headless 空实现（headless/GUI 同用；GUI 真实现归 A2b）。
// 对话框返回"未选择"，消息/进度/输出通道均为 no-op，不触碰任何 QWidget。
// state() 持有一份会话状态 hub（纯数据、可序列化，headless 下也可安全读写）。

#include "IWindow.h"
#include "PersistentStateHub.h"

namespace nextpilot::plotscope::workbench
{

class NullWindow : public IWindow
{
public:
    std::optional<QString> showOpenDialog(const QString &, const QString & = {},
                                          const QString & = {}) override
    {
        return std::nullopt;
    }
    std::optional<QString> showSaveDialog(const QString &, const QString & = {},
                                          const QString & = {}) override
    {
        return std::nullopt;
    }
    void showInformationMessage(const QString &) override {}
    void showWarningMessage(const QString &) override {}
    void showErrorMessage(const QString &) override {}
    void withProgress(const QString &,
                      const std::function<void(int, const QString &)> &report) override
    {
        if (report)
            report(100, QString());
    }
    std::optional<int> showQuickPick(const QStringList &, const QString &) override
    {
        return std::nullopt;
    }
    std::optional<QString> showInputBox(const QString &, const QString & = {}) override
    {
        return std::nullopt;
    }

    int activeSource() const override { return -1; }
    void showSource(int) override {}
    void setActiveChannel(int, const QString &, const QString &) override {}

    nextpilot::plotscope::context::PersistentStateHub &state() override { return m_state; }

private:
    nextpilot::plotscope::context::PersistentStateHub m_state;
};

} // namespace nextpilot::plotscope::workbench
