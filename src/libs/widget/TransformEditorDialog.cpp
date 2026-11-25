#include "TransformEditorDialog.h"

#include "AppContext.h"
#include "TransformField.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui {

TransformEditorDialog::TransformEditorDialog(AppContext &appCtx, uint32_t capability, const QString &contextTitle,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(
        QStringLiteral("变换 · %1").arg(contextTitle.isEmpty() ? capabilityNames(capability) : contextTitle));
    resize(560, 480);

    auto *root = new QVBoxLayout(this);
    m_field = new TransformField(appCtx, capability, this);
    root->addWidget(m_field);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

TransformEditorDialog::~TransformEditorDialog() = default;

QJsonObject TransformEditorDialog::currentRequest() const { return m_field->request(); }

int TransformEditorDialog::descriptorCount() const { return m_field->descriptorCount(); }

QJsonObject TransformEditorDialog::run(QWidget *parent, AppContext &appCtx, uint32_t capability,
                                       const QString &contextTitle, const QJsonObject &initial)
{
    TransformEditorDialog dlg(appCtx, capability, contextTitle, parent);
    if (!initial.isEmpty())
        dlg.m_field->setRequest(initial);
    if (dlg.exec() != QDialog::Accepted)
        return QJsonObject();
    return dlg.currentRequest();
}

} // namespace nextpilot::plotscope::gui
