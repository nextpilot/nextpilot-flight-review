#pragma once

#include "IIntentExecutor.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

// 实时源连接对话框：传输 / 端点 / 波特率 / 解析器 / 模板，一次填完直接发 data.connect。
//
// 为什么单独成对话框而不是塞进数据树：连接参数是"多字段 + 互斥"的形态（UDP/TCP 要端口、
// 串口要设备与波特率、custom-protocol 要模板），塞进树里就变成一串没上下文的菜单项。
//
// 提交走意图层（data.connect），本类不碰 socket——与 headless 路径完全同一条出口。
namespace nextpilot::plotscope::gui
{

class LiveConnectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LiveConnectDialog(nextpilot::plotscope::intent::IIntentExecutor &executor,
                               QWidget *parent = nullptr);

    // 成功连接后返回实际使用的 url（供调用方写日志/回填）；未连接返回空。
    QString connectedUrl() const { return m_connectedUrl; }

private:
    enum Transport { Udp = 0, Tcp = 1, Serial = 2 };

    void buildUi();
    void updateFieldVisibility();
    void updateUrlPreview();
    QString composeUrl() const;
    void submit();

    nextpilot::plotscope::intent::IIntentExecutor &m_executor;

    QComboBox *m_transport = nullptr;
    QLineEdit *m_host = nullptr;   // UDP/TCP 的 host（0.0.0.0 = 监听）
    QSpinBox *m_port = nullptr;
    QLineEdit *m_device = nullptr; // 串口设备
    QPushButton *m_browseDev = nullptr;
    QComboBox *m_baud = nullptr;
    QComboBox *m_parser = nullptr;
    QLineEdit *m_template = nullptr;
    QPushButton *m_browseTpl = nullptr;
    QLabel *m_urlPreview = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_ok = nullptr;

    QWidget *m_deviceRow = nullptr;
    QWidget *m_hostRow = nullptr;
    QWidget *m_portRow = nullptr;
    QWidget *m_baudRow = nullptr;
    QWidget *m_templateRow = nullptr;

    QString m_connectedUrl;
};

} // namespace nextpilot::plotscope::gui
