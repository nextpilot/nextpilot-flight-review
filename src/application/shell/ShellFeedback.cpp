#include "ShellFeedback.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>

ShellFeedback::ShellFeedback(QMainWindow *window)
    : m_window(window)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    QDir().mkpath(dir + QStringLiteral("/logs"));
    m_logPath = dir + QStringLiteral("/logs/plotscope.log");
}

void ShellFeedback::attach(QStatusBar *statusBar, QPlainTextEdit *logView)
{
    m_statusBar = statusBar;
    setLogView(logView);
}

void ShellFeedback::setLogView(QPlainTextEdit *logView)
{
    m_logView = logView;
    if (!m_logView)
        return;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
}

void ShellFeedback::info(const QString &text)
{
    write(text);
}

void ShellFeedback::warn(const QString &text)
{
    write(QStringLiteral("警告：%1").arg(text));
}

void ShellFeedback::error(const QString &title, const QString &detail)
{
    write(QStringLiteral("错误：%1").arg(detail));
    if (m_window)
        QMessageBox::critical(m_window, title, detail);
}

void ShellFeedback::write(const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(ts, text);
    if (m_logView)
        m_logView->appendPlainText(line);
    if (m_statusBar)
        m_statusBar->showMessage(text, 4000);
    appendFile(line);
}

void ShellFeedback::appendFile(const QString &text) const
{
    QFile file(m_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << text << '\n';
}
