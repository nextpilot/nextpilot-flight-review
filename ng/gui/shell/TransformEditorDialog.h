#pragma once

#include "AppContext.h"
#include <QDialog>
#include <QJsonObject>

#include "Transform.h"

namespace nextpilot::plotscope::gui
{

class TransformField;

// 变换输入对话框：承载一个 TransformField + Ok/Cancel。
// 控件本体在 TransformField，可嵌入任意面板；这里只是给"弹窗配置"场景用的薄壳。
class TransformEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TransformEditorDialog(nextpilot::plotscope::context::AppContext &appCtx,
                                   uint32_t capability,
                                   const QString &contextTitle = QString(),
                                   QWidget *parent = nullptr);
    ~TransformEditorDialog() override;

    static QJsonObject run(QWidget *parent, nextpilot::plotscope::context::AppContext &appCtx,
                           uint32_t capability, const QString &contextTitle = QString(),
                           const QJsonObject &initial = QJsonObject());

    QJsonObject currentRequest() const;
    int descriptorCount() const;

private:
    TransformField *m_field = nullptr;
};

} // namespace nextpilot::plotscope::gui
