#pragma once

#include <QString>

class QMainWindow;
class QPlainTextEdit;
class QStatusBar;

// 统一反馈出口：日志视图、状态栏和落盘日志。
class ShellFeedback
{
public:
    explicit ShellFeedback(QMainWindow *window);

    void attach(QStatusBar *statusBar, QPlainTextEdit *logView);
    void setLogView(QPlainTextEdit *logView);
    void info(const QString &text);
    void warn(const QString &text);
    void error(const QString &title, const QString &detail);
    QString logPath() const { return m_logPath; }

private:
    void write(const QString &text);
    void appendFile(const QString &text) const;

    QMainWindow *m_window = nullptr;
    QStatusBar *m_statusBar = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QString m_logPath;
};
