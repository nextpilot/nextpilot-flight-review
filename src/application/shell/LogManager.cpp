#include "LogManager.h"
#include "Component.h"
#include "ShellPaths.h"

#include <QDateTime>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QTextStream>

ps::components::DockContribution LogManager::dockContribution()
{
    using namespace ps::components;
    return DockContribution{QStringLiteral("dock.log"), QStringLiteral("日志"),
                            Qt::BottomDockWidgetArea, true, 5,
                            QStringLiteral(":/icons/log.svg"),
                            [](QWidget *parent) -> QWidget * {
                                auto *dock = new QDockWidget(QStringLiteral("日志"), parent);
                                dock->setObjectName(QStringLiteral("dock.log"));
                                dock->setProperty("psIcon", QStringLiteral(":/icons/log.svg"));
                                auto *view = new QPlainTextEdit(dock);
                                view->setObjectName(QStringLiteral("dock.log.view"));
                                dock->setWidget(view);
                                return dock;
                            },
                            {}};
}

LogManager::LogManager(QMainWindow *window)
    : m_window(window)
{
    QDir().mkpath(ShellPaths::logDir());
    m_logPath = ShellPaths::logPath();
}

void LogManager::setStatusBar(QStatusBar *statusBar)
{
    m_statusBar = statusBar;
}

void LogManager::setLogView(QPlainTextEdit *logView)
{
    m_logView = logView;
    if (!m_logView)
        return;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
}

void LogManager::addSink(Sink sink)
{
    if (sink)
        m_sinks.append(std::move(sink));
}

void LogManager::info(const QString &text)
{
    write(text, Level::Info);
}

void LogManager::warn(const QString &text)
{
    write(QStringLiteral("警告：%1").arg(text), Level::Warning);
}

void LogManager::error(const QString &title, const QString &detail)
{
    write(QStringLiteral("错误：%1").arg(detail), Level::Error);
    if (m_window)
        QMessageBox::critical(m_window, title, detail);
}

void LogManager::write(const QString &text, Level level)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(ts, text);
    if (m_logView)
        m_logView->appendPlainText(line);
    if (m_statusBar)
        m_statusBar->showMessage(text, 4000);
    appendFile(line);
    for (const Sink &sink : m_sinks)
        sink(line, level);
}

void LogManager::appendFile(const QString &line) const
{
    QFile file(m_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << line << '\n';
}
