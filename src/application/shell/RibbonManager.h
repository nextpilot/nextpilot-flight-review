#pragma once

class QMainWindow;
class SARibbonBar;
class QObject;
class QWidget;

namespace ps::commands
{
class CommandManager;
}
namespace ps::components
{
class Registry;
}

class RibbonManager
{
public:
    RibbonManager(QMainWindow *window, ps::commands::CommandManager *commands,
                  const ps::components::Registry *registry, QObject *eventFilter);
    QWidget *create();
    void applyTheme();

private:
    QMainWindow *m_window = nullptr;
    ps::commands::CommandManager *m_commands = nullptr;
    const ps::components::Registry *m_registry = nullptr;
    QObject *m_filter = nullptr;
    SARibbonBar *m_bar = nullptr;
};
