#pragma once

#include "AppContext.h"
#include "FormSchema.h"

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QComboBox;
class QFormLayout;
class QLabel;
class QWidget;

namespace nextpilot::plotscope::gui
{

// 过滤器编辑器：从 FilterOps 拿 descriptor 列表（下拉选 + 说明 + 参数表单），
// 输出 { filter:<id>, params:{...} } 供 data.filter 意图直接消费。
// 只用 AppContext 的 JSON 能力，不依赖 script 模块类型。
class FilterEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilterEditorDialog(nextpilot::plotscope::context::AppContext &appCtx,
                                const QString &contextTitle, QWidget *parent = nullptr);
    ~FilterEditorDialog() override;

    // 当前选择：{ filter, params }。无有效选择时 filter 为空。
    QJsonObject currentRequest() const;
    int descriptorCount() const;
    // 按 id 选中指定过滤器（供截图/脚本精确定位）；不存在返回 false。
    bool setCurrentFilter(const QString &id);

    // 便捷弹窗：取消返回空 QJsonObject。
    static QJsonObject run(QWidget *parent, nextpilot::plotscope::context::AppContext &appCtx,
                           const QString &contextTitle);

private Q_SLOTS:
    void onCurrentChanged(int index);

private:
    void rebuildParamWidgets();
    QJsonObject currentDescriptor() const;

    nextpilot::plotscope::context::AppContext &m_appCtx;
    QJsonArray m_descriptors;
    QComboBox *m_combo = nullptr;
    QLabel *m_description = nullptr;
    QWidget *m_paramHost = nullptr;
    nextpilot::plotscope::uischema::FormSchema m_paramForm;
};

} // namespace nextpilot::plotscope::gui
