#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "FormSchema.h"
#include "IMessageParser.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTreeWidget;
class QWidget;

namespace nextpilot::plotscope::protocol
{

// 专用协议检查对话框——消费 IMessageParser 的自述信息，不认识任何具体协议。
//
// 与 ImportDialog（数据打开对话框）刻意分开：
//   - ImportDialog 消费 IMessageParser，产出 data.load 意图 → Dataset（画图）；
//   - 本对话框消费 IMessageParser，直接 parse → DecodedFrame（逐帧查看字段/校验）。
//   两者用途不同，合并会让"打开"语义分裂（用户上一轮明确选择新建专用对话框）。
//
// 自述驱动（加一个协议只写它自己的类，本对话框不动）：
//   protocolName / domain / usage / supportedTransports → 左侧列表与说明；
//   configSchema → 右上参数控件（configure 只从这些参数取值，不读全局）；
//   parse → 右下字段树。
class ProtocolInspectorDialog : public QDialog
{
    Q_OBJECT
public:
    // parsers 是解析目录快照（IMessageParser* 抽象接口列表），由 AppContext::parsers() 提供；
    // 对话框不 include ToolboxHub、不认识工具箱容器。
    explicit ProtocolInspectorDialog(const QList<nextpilot::plotscope::parser::IMessageParser *> &parsers,
                                     QWidget *parent = nullptr);
    ~ProtocolInspectorDialog() override;

    static void run(const QList<nextpilot::plotscope::parser::IMessageParser *> &parsers, QWidget *parent = nullptr);

private:
    void rebuildProtocolList();
    void onProtocolChanged();
    void rebuildParamFields();
    void chooseFile();
    void doParse();
    QJsonObject collectParams() const;
    void applyTheme();

    QList<nextpilot::plotscope::parser::IMessageParser *> m_parsers;
    nextpilot::plotscope::parser::IMessageParser *m_current = nullptr;

    QListWidget *m_protocolList = nullptr;
    QLabel *m_info = nullptr;
    QComboBox *m_transport = nullptr;
    QWidget *m_paramHost = nullptr;
    QFormLayout *m_paramForm = nullptr;
    QLineEdit *m_path = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QTreeWidget *m_fields = nullptr;
    QLabel *m_status = nullptr;

    nextpilot::plotscope::uischema::FormSchema m_paramFormModel;
};

} // namespace nextpilot::plotscope::protocol
