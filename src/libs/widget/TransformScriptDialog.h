#pragma once

#include "AppContext.h"
#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace nextpilot::plotscope::gui {

// Lua 编辑器：代码编辑 + 单点/批量在线调试 + 保存到 scripts/ops 并注册。
// 调试逻辑从旧 TransformEditorDialog 抽出复用，不重写。
class TransformScriptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TransformScriptDialog(nextpilot::plotscope::context::AppContext &appCtx, QWidget *parent = nullptr);
    ~TransformScriptDialog() override;

    // 载入已有脚本或内联代码。scriptId 非空表示脚本库引用。
    void loadScript(const QString &scriptId, const QString &source, const QString &name);

    // 返回保存后的请求（descriptorId=lua.<name> + scriptId + luaSource 快照）。
    QJsonObject savedRequest() const;

private Q_SLOTS:
    void onSave();
    void onDebugSingle();
    void onDebugBatch();
    void onFillFromActiveChannel();

private:
    void showResult(const QString &text);
    QJsonObject currentRequest() const;

    nextpilot::plotscope::context::AppContext &m_appCtx;
    QComboBox *m_name = nullptr;
    QLineEdit *m_label = nullptr;
    QPlainTextEdit *m_code = nullptr;
    QLineEdit *m_singleX = nullptr;
    QLineEdit *m_singleT = nullptr;
    QPlainTextEdit *m_batch = nullptr;
    QLabel *m_result = nullptr;
    QString m_loadedScriptId;
    QJsonObject m_savedRequest;
};

} // namespace nextpilot::plotscope::gui
