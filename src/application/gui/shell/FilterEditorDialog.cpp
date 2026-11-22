#include "FilterEditorDialog.h"

#include "AppContext.h"
#include "InteractionHub.h"

#include "FormSchema.h"
#include "ParamSpec.h"
#include "QtFieldRenderer.h"
#include "SchemaEngine.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui
{

FilterEditorDialog::FilterEditorDialog(AppContext &appCtx, const QString &contextTitle, QWidget *parent)
    : QDialog(parent), m_appCtx(appCtx)
{
    setWindowTitle(QStringLiteral("过滤器 · %1").arg(
        contextTitle.isEmpty() ? QStringLiteral("通道") : contextTitle));
    resize(520, 460);

    if (m_appCtx.interactions().hasFilterOps())
        m_descriptors = m_appCtx.interactions().filterOps().list();

    auto *root = new QVBoxLayout(this);
    m_combo = new QComboBox(this);
    for (const QJsonValue &jv : m_descriptors)
    {
        const QJsonObject d = jv.toObject();
        const QString name = d.value(QStringLiteral("name")).toString();
        m_combo->addItem(name.isEmpty() ? d.value(QStringLiteral("id")).toString() : name,
                         d.value(QStringLiteral("id")).toString());
    }
    root->addWidget(new QLabel(QStringLiteral("选择过滤器："), this));
    root->addWidget(m_combo);

    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    root->addWidget(m_description);

    m_paramHost = new QWidget(this);
    root->addWidget(m_paramHost);
    root->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FilterEditorDialog::onCurrentChanged);

    onCurrentChanged(m_combo->currentIndex());
}

FilterEditorDialog::~FilterEditorDialog() = default;

int FilterEditorDialog::descriptorCount() const
{
    return m_descriptors.size();
}

bool FilterEditorDialog::setCurrentFilter(const QString &id)
{
    if (!m_combo)
        return false;
    const int idx = m_combo->findData(id);
    if (idx < 0)
        return false;
    m_combo->setCurrentIndex(idx);
    return true;
}

QJsonObject FilterEditorDialog::currentDescriptor() const
{
    const int idx = m_combo ? m_combo->currentIndex() : -1;
    if (idx < 0 || idx >= m_descriptors.size())
        return QJsonObject();
    return m_descriptors.at(idx).toObject();
}

namespace
{
// descriptor 参数 JSON → ParamSpec（字段名在这里一次性对齐 SchemaEngine 约定）。
ParamSpec toParamSpec(const QJsonObject &po)
{
    ParamSpec s;
    s.key = po.value(QStringLiteral("name")).toString();
    const QString type = po.value(QStringLiteral("type")).toString();
    s.type = paramTypeFromName(type);
    s.label = po.value(QStringLiteral("label")).toString();
    s.description = po.value(QStringLiteral("tooltip")).toString();
    s.unit = po.value(QStringLiteral("unit")).toString();
    s.defaultValue = po.value(QStringLiteral("default")).toVariant();
    if (po.contains(QStringLiteral("min")) && po.contains(QStringLiteral("max")))
    {
        s.hasRange = true;
        s.min = po.value(QStringLiteral("min")).toDouble();
        s.max = po.value(QStringLiteral("max")).toDouble();
    }
    if (po.contains(QStringLiteral("decimals")))
        s.precision = po.value(QStringLiteral("decimals")).toInt();
    for (const QJsonValue &ov : po.value(QStringLiteral("values")).toArray())
    {
        const QJsonObject e = ov.toObject();
        s.options.append(EnumOption{e.value(QStringLiteral("value")).toString(),
                                    e.value(QStringLiteral("label")).toString()});
    }
    return s;
}
} // namespace

void FilterEditorDialog::rebuildParamWidgets()
{
    QFormLayout *form = qobject_cast<QFormLayout *>(m_paramHost->layout());
    if (!form)
    {
        form = new QFormLayout(m_paramHost);
    }
    else
    {
        while (form->count() > 0)
        {
            QLayoutItem *item = form->takeAt(0);
            if (!item)
                break;
            if (QWidget *w = item->widget()) { w->setParent(nullptr); delete w; }
            delete item;
        }
    }

    const QJsonObject d = currentDescriptor();
    const QJsonArray params = d.value(QStringLiteral("parameters")).toArray();
    QVector<ParamSpec> specs;
    for (const QJsonValue &pv : params)
        if (pv.isObject()) specs.append(toParamSpec(pv.toObject()));

    m_paramForm = SchemaEngine::compile(d.value(QStringLiteral("name")).toString(), specs);
    if (m_paramForm.empty())
        form->addRow(new QLabel(QStringLiteral("（该过滤器无参数）"), m_paramHost));
    else
        QtFieldRenderer::render(m_paramForm, form, m_paramHost);
}

void FilterEditorDialog::onCurrentChanged(int)
{
    const QJsonObject d = currentDescriptor();
    m_description->setText(QStringLiteral("%1\n版本：%2　输出：%3")
                               .arg(d.value(QStringLiteral("description")).toString(),
                                    d.value(QStringLiteral("version")).toString(),
                                    d.value(QStringLiteral("outputKind")).toString()));
    rebuildParamWidgets();
}

QJsonObject FilterEditorDialog::currentRequest() const
{
    const QJsonObject d = currentDescriptor();
    const QString id = d.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return QJsonObject();

    QJsonObject params;
    // 用 fieldKey 动态属性遍历参数控件，按 key 收集当前值。
    const auto widgets = m_paramHost->findChildren<QWidget *>();
    for (QWidget *w : widgets)
    {
        const QString key = QtFieldRenderer::keyOf(w);
        if (key.isEmpty())
            continue;
        const QVariant v = QtFieldRenderer::valueOf(w);
        if (v.isValid())
            params.insert(key, QJsonValue::fromVariant(v));
    }
    return QJsonObject{{QStringLiteral("filter"), id}, {QStringLiteral("params"), params}};
}

QJsonObject FilterEditorDialog::run(QWidget *parent, AppContext &appCtx, const QString &contextTitle)
{
    FilterEditorDialog dlg(appCtx, contextTitle, parent);
    if (dlg.exec() != QDialog::Accepted)
        return QJsonObject();
    return dlg.currentRequest();
}

} // namespace nextpilot::plotscope::gui
