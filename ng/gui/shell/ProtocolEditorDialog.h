#pragma once

#include "AppContext.h"
#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QWidget;

namespace nextpilot::plotscope::gui
{

// 协议编辑器：创建/编辑/校验顺序段协议模板（.psproto），并试算样例帧。
//
// 这是"工具"不是"器官"：形态是整篇 JSON 文档 + 校验 + 试算，
// 与属性面板（少量键值行）形态不同，故不用 PropertySchema 契约。
// 逻辑全部来自 ng/libs/protocol（validate / parseDocument / previewSequentialFrame），
// 对话框只是这些纯逻辑的 GUI 外壳——不复制任何解码规则。
//
// 与老 ProtocolEditorDialog（1749 行）的差别：老版把 JSON 解析、表达式编辑、
// 位域模型、CRC 试算全塞进对话框；这里逻辑下沉到共享库，编辑器只做三件事：
// 编辑文本、显示校验结果、显示试算字段。协议保存后可直接被 SPI 解析器加载。
class ProtocolEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProtocolEditorDialog(QWidget *parent = nullptr,
                                  nextpilot::plotscope::context::AppContext *appCtx = nullptr);
    ~ProtocolEditorDialog() override;

    static void run(QWidget *parent = nullptr,
                    nextpilot::plotscope::context::AppContext *appCtx = nullptr);

    // 空白模板（供"新建"与测试使用）。
    static QString blankTemplate();

private:
    void newDoc();
    void openDoc();
    void saveDoc();
    void validateDoc();
    void previewFrame();
    void applyTheme();
    void configureChecksumTransform();

    nextpilot::plotscope::context::AppContext *m_appCtx = nullptr;
    QJsonObject m_checksumRequest;  // checksum 变换配方（算子/表达式/Lua）
    QString m_checksumLabel;
    QString m_path;
    QPlainTextEdit *m_json = nullptr;
    QPlainTextEdit *m_hex = nullptr;
    QLabel *m_validation = nullptr;
    QTreeWidget *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_saveBtn = nullptr;
};

} // namespace nextpilot::plotscope::gui
