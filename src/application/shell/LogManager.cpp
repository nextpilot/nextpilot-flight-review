#include "LogManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>

LogManager::LogManager(QMainWindow *window)
    : m_window(window)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    QDir().mkpath(dir + QStringLiteral("/logs"));
    m_logPath = dir + QStringLiteral("/logs/plotscope.log");
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
