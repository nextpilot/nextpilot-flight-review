#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
struct DataInputEntry;

// 通用设备调试终端：扩展只提供 debug schema + status/send 回调。
//   schema.debug.commands: 预置命令 [{label, command}]
//   schema.debug.status:   状态字段 [{key, label, type?, suffix?, true?, false?}]
//   schema.debug.hint:     顶部说明
// 未提供 debug 段时，兼容读取 schema.commands / schema.hint，并自动平铺状态 JSON 的标量字段。
class DeviceDebugDialog : public QDialog
{
    Q_OBJECT
public:
    DeviceDebugDialog(const DataInputEntry &source, const QString &instanceName,
                      std::function<QString()> statusFn,
                      std::function<QString(const QString &)> sendFn,
                      QWidget *parent = nullptr);

private:
    void sendCurrent();
    void refreshStatus();
    void append(const QString &text);
    QString statusValueText(const QJsonValue &value, const QJsonObject &field) const;

    QComboBox *m_presets = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_send = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_state = nullptr;
    std::function<QString()> m_statusFn;
    std::function<QString(const QString &)> m_sendFn;
    QJsonArray m_statusFields;
    QString m_lastRx;
    bool m_hasCommand = false;
};
