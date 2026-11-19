#pragma once

#include "AppContext.h"
#include <QJsonObject>
#include <QWidget>

#include "FormSchema.h"
#include "Transform.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

namespace nextpilot::plotscope::core
{
}

namespace nextpilot::plotscope::gui
{

using nextpilot::plotscope::core::TransformCapability;
using nextpilot::plotscope::core::TransformDescriptor;
using nextpilot::plotscope::core::TransformKind;

// 变换输入控件：可编辑下拉（选算子看说明 / 直接敲表达式）+ 参数区 + 右侧编辑按钮。
// 输出同一份 TransformRequest JSON，求值器零改动。凡要输入变换处放一个它。
class TransformField : public QWidget
{
    Q_OBJECT
public:
    explicit TransformField(nextpilot::plotscope::core::AppContext &appCtx,
                            uint32_t capability, QWidget *parent = nullptr);
    ~TransformField() override;

    QJsonObject request() const;
    void setRequest(const QJsonObject &initial);

    void setLabel(const QString &label);
    int descriptorCount() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void onCurrentChanged(int index);
    void onTextEdited(const QString &text);
    void onEditLua();

private:
    void rebuildDescriptors();
    void rebuildParamWidgets();
    void applyDescriptor(const TransformDescriptor &d, const QJsonObject &initial);
    void refreshScripts();
    QString currentDescriptorId() const;

    nextpilot::plotscope::core::AppContext &m_appCtx;
    uint32_t m_capability = 0;
    QList<TransformDescriptor> m_descriptors;
    QJsonObject m_lastLuaRequest; // 编辑按钮回填的 Lua 请求

    QComboBox *m_combo = nullptr;
    QLabel *m_description = nullptr;
    QWidget *m_paramHost = nullptr;
    QPlainTextEdit *m_expression = nullptr;
    QPushButton *m_editLua = nullptr;
    nextpilot::plotscope::uischema::FormSchema m_paramForm;
    bool m_updating = false;
};

} // namespace nextpilot::plotscope::gui
