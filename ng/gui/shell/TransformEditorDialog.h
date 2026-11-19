#pragma once

#include <QDialog>
#include <QJsonObject>

#include "Transform.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QWidget;

namespace nextpilot::plotscope::core
{
class AppContext;
}

namespace nextpilot::plotscope::gui
{

using nextpilot::plotscope::core::TransformDescriptor;
using nextpilot::plotscope::core::TransformKind;

// 变换编辑器 + 在线调试：三类变换（内置算子 / 数学表达式 / Lua）的统一配置入口。
//
// 防呆（用户原话："选错了就死了"）：
//   · 打开时给定 cap（Derived/AxisX/AxisY/Checksum），下拉框只列 supports(cap) 的
//     descriptor，能力不匹配的根本选不到；
//   · 每个 descriptor 展示自说明（description）+ 示例 + 参数说明；
//   · 运行时 evaluator 再校验一次能力（双保险）。
//
// 在线调试：
//   · 单点（live）：输入 x/t -> 调用统一 evaluator -> 显示 y；
//   · 批量（offline）：粘贴一列数值（或点"取当前活动通道"）-> 全部求值 -> 预览前若干行。
//
// 它不含任何 script 类：求值走 AppContext::evalTransform（由 script 模块注入实现），
// 因此能放在通用 gui 层，数据源/曲线/协议三处都能打开。
class TransformEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TransformEditorDialog(nextpilot::plotscope::core::AppContext &appCtx,
                                   uint32_t capability,
                                   const QString &contextTitle = QString(),
                                   QWidget *parent = nullptr);
    ~TransformEditorDialog() override;

    // 返回一份 TransformRequest JSON（descriptorId/kind/expression/luaSource/params）；
    // 用户取消返回空对象。
    static QJsonObject run(QWidget *parent, nextpilot::plotscope::core::AppContext &appCtx,
                           uint32_t capability, const QString &contextTitle = QString(),
                           const QJsonObject &initial = QJsonObject());

    // 诊断/测试用。
    QJsonObject currentRequest() const;
    int descriptorCount() const;
    QString lastResult() const;

private Q_SLOTS:
    void onDescriptorChanged(int index);
    void onDebugSingle();
    void onDebugBatch();
    void onFillFromActiveChannel();
    void onAccept();

private:
    void rebuildParamWidgets();
    void applyDescriptor(const TransformDescriptor &d, const QJsonObject &initial);
    QJsonObject collectRequest() const;
    QJsonObject transformRequestJson(const QString &id, TransformKind kind,
                                     const QString &expr, const QString &lua,
                                     const QJsonObject &params) const;

    nextpilot::plotscope::core::AppContext &m_appCtx;
    uint32_t m_capability = 0;
    QList<TransformDescriptor> m_descriptors;

    QComboBox *m_descriptorCombo = nullptr;
    QLabel *m_description = nullptr;
    QWidget *m_paramHost = nullptr;
    QPlainTextEdit *m_expression = nullptr;
    QPlainTextEdit *m_lua = nullptr;
    QLineEdit *m_singleX = nullptr;
    QLineEdit *m_singleT = nullptr;
    QPlainTextEdit *m_batch = nullptr;
    QLabel *m_result = nullptr;
    QString m_lastResult;
};

} // namespace nextpilot::plotscope::gui
