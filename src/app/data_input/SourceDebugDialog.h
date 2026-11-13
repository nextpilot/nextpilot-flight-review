#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// 通用 source 调试终端：插件只提供一个命令 schema + send 回调，
// app 不认识 OEM7 / MAVLink 的具体命令文本。
class SourceDebugDialog : public QDialog
{
    Q_OBJECT
public:
    SourceDebugDialog(const QString &title, const QJsonObject &schema,
                      std::function<QString()> statusFn,
                      std::function<QString(const QString &)> sendFn,
                      QWidget *parent = nullptr);

private:
    void sendCurrent();
    void refreshStatus();
    void append(const QString &text);

    QComboBox *m_presets = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_send = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_state = nullptr;
    std::function<QString()> m_statusFn;
    std::function<QString(const QString &)> m_sendFn;
    QString m_lastRx;
    bool m_hasCommand = false;
};
