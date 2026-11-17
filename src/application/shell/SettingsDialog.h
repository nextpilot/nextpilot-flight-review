#pragma once

#include <QDialog>
#include <functional>
#include <QVector>

class QWidget;
class ExtensionManager;
class QListWidget;
class QStackedWidget;

// 全局设置对话框：左侧分类，右侧模块设置页。
// 模块通过 Registry::addSettings() 贡献页面，shell 不认识的业务设置也能挂进来。
class SettingsDialog : public QDialog
{
public:
    SettingsDialog(QWidget *parent, ExtensionManager &extensions, std::function<void()> applyTheme);

private:
    void addPage(const QString &title, const QString &icon, QWidget *page,
                 std::function<void()> apply);
    void applyAll();

    QWidget *m_host = nullptr;
    ExtensionManager &m_extensions;
    std::function<void()> m_applyTheme;
    QListWidget *m_list = nullptr;
    QStackedWidget *m_stack = nullptr;
    QVector<std::function<void()>> m_appliers;
};
