#pragma once

#include <QString>
#include <QVector>

#include <functional>

class QMainWindow;
class QPlainTextEdit;
class QStatusBar;

// 日志管理：统一收发日志，Dock 视图、状态栏、文件都是 sink。
class LogManager
{
public:
    enum class Level
    {
        Info,
        Warning,
        Error
    };

    using Sink = std::function<void(const QString &line, Level level)>;

    explicit LogManager(QMainWindow *window);

    void setStatusBar(QStatusBar *statusBar);
    void setLogView(QPlainTextEdit *logView);
    void addSink(Sink sink);

    void info(const QString &text);
    void warn(const QString &text);
    void error(const QString &title, const QString &detail);
    QString logPath() const { return m_logPath; }

private:
    void write(const QString &text, Level level);
    void appendFile(const QString &line) const;

    QMainWindow *m_window = nullptr;
    QStatusBar *m_statusBar = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QString m_logPath;
    QVector<Sink> m_sinks;
};
