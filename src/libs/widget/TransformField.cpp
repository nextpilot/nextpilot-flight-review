#include "TransformField.h"

#include "AppContext.h"
#include "TransformScriptDialog.h"
#include "QtFieldRenderer.h"
#include "Result.h"
#include "SchemaEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <QJsonValue>

using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui {

TransformField::TransformField(AppContext &appCtx, uint32_t capability, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_capability(capability)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout;
    m_combo = new QComboBox(this);
    m_combo->setEditable(true);
    top->addWidget(m_combo, 1);
    m_editLua = new QPushButton(QStringLiteral("编辑…"), this);
    m_editLua->setToolTip(QStringLiteral("在 Lua 编辑器中编写/调试并保存脚本"));
    top->addWidget(m_editLua);
    root->addLayout(top);

    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setObjectName(QStringLiteral("textDim"));
    root->addWidget(m_description);

    m_expression = new QPlainTextEdit(this);
    m_expression->setMaximumHeight(64);
    m_expression->setPlaceholderText(QStringLiteral("数学表达式，如 5*x+10/sin(x)"));
    root->addWidget(m_expression);

    m_paramHost = new QWidget(this);
    root->addWidget(m_paramHost);

    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TransformField::onCurrentChanged);
    connect(m_combo, &QComboBox::editTextChanged, this, &TransformField::onTextEdited);
    connect(m_editLua, &QPushButton::clicked, this, &TransformField::onEditLua);

    rebuildDescriptors();
}

TransformField::~TransformField() = default;

int TransformField::descriptorCount() const { return m_descriptors.size(); }

void TransformField::setLabel(const QString &label) { m_description->setText(label); }

void TransformField::rebuildDescriptors()
{
    m_updating = true;
    m_descriptors = m_appCtx.transforms().byCapability(static_cast<TransformCapability>(m_capability));
    m_combo->clear();
    for (const TransformDescriptor &d : m_descriptors)
    {
        m_combo->addItem(QStringLiteral("%1 · %2").arg(d.label, d.description), d.id);
    }
    m_updating = false;
    if (!m_descriptors.isEmpty())
        onCurrentChanged(0);
}

void TransformField::refreshScripts()
{
    const QString prev = currentDescriptorId();
    rebuildDescriptors();
    const int idx = m_combo->findData(prev);
    if (idx >= 0)
        m_combo->setCurrentIndex(idx);
}

QString TransformField::currentDescriptorId() const { return m_combo->currentData().toString(); }

void TransformField::onCurrentChanged(int index)
{
    if (m_updating || index < 0 || index >= m_descriptors.size())
        return;
    applyDescriptor(m_descriptors.at(index), QJsonObject());
    Q_EMIT changed();
}

void TransformField::onTextEdited(const QString &text)
{
    if (m_updating)
        return;
    // 文本不再匹配任何下拉项 -> 视为直接输入的表达式。
    for (const TransformDescriptor &d : m_descriptors)
    {
        if (text == d.label || text == d.id || text.startsWith(d.label + QStringLiteral(" ·")))
            return;
    }
    const int idx = m_combo->findData(QStringLiteral("expression"));
    if (idx >= 0 && m_combo->currentIndex() != idx)
    {
        m_updating = true;
        m_combo->setCurrentIndex(idx);
        m_combo->setEditText(text);
        m_updating = false;
    }
    m_expression->setPlainText(text);
    m_description->setText(QStringLiteral("直接输入数学表达式"));
    Q_EMIT changed();
}

void TransformField::applyDescriptor(const TransformDescriptor &d, const QJsonObject &initial)
{
    m_updating = true;
    m_description->setText(
        QStringLiteral("%1\n示例：%2").arg(d.description, d.example.isEmpty() ? QStringLiteral("—") : d.example));
    const TransformRequest init = TransformRequest::fromJson(initial);

    const bool isExpr = d.kind == TransformKind::Expression;
    const bool isLua = d.kind == TransformKind::Lua;
    m_expression->setVisible(isExpr);
    m_editLua->setVisible(isLua);
    m_paramHost->setVisible(!isExpr && !isLua);

    if (!initial.isEmpty() && init.kind == d.kind)
    {
        if (isExpr)
            m_expression->setPlainText(init.expression);
        if (isLua)
            m_lastLuaRequest = initial;
    }
    else
    {
        if (isExpr)
            m_expression->setPlainText(d.example);
        if (isLua)
            m_lastLuaRequest = QJsonObject();
    }

    if (!isExpr && !isLua)
        rebuildParamWidgets();

    // 回填 builtin 参数值。
    if (!isExpr && !isLua)
    {
        const auto widgets = m_paramHost->findChildren<QWidget *>();
        for (QWidget *w : widgets)
        {
            const QString key = QtFieldRenderer::keyOf(w);
            if (key.isEmpty())
                continue;
            const QJsonValue jv = init.params.value(key);
            QVariant v;
            if (!jv.isUndefined() && !jv.isNull())
                v = jv.toVariant();
            QVariant fallback;
            for (const FieldSpec &f : m_paramForm.fields)
                if (f.key == key)
                    fallback = f.defaultValue;
            QtFieldRenderer::setValue(w, v.isValid() ? v : fallback);
        }
    }
    m_updating = false;
}

void TransformField::rebuildParamWidgets()
{
    if (QLayout *old = m_paramHost->layout())
    {
        while (QLayoutItem *item = old->takeAt(0))
        {
            if (QWidget *w = item->widget())
            {
                w->setParent(nullptr);
                delete w;
            }
            delete item;
        }
        delete old;
    }
    auto *form = new QFormLayout(m_paramHost);
    const int idx = m_combo->currentIndex();
    if (idx < 0 || idx >= m_descriptors.size())
        return;
    const TransformDescriptor &d = m_descriptors.at(idx);

    QHash<QString, QString> labels;
    for (const TransformParam &p : d.params)
        labels.insert(p.key, p.label);
    m_paramForm = SchemaEngine::compile(d.label, transformParamSpecs(d), labels);

    if (m_paramForm.empty())
    {
        form->addRow(new QLabel(QStringLiteral("（该变换无参数）"), m_paramHost));
        return;
    }
    QtFieldRenderer::render(m_paramForm, form, m_paramHost);
}

void TransformField::setRequest(const QJsonObject &initial)
{
    if (initial.isEmpty())
        return;
    const QString id = TransformRequest::fromJson(initial).descriptorId;
    const int idx = m_combo->findData(id);
    if (idx >= 0)
    {
        const QSignalBlocker block(m_combo);
        m_combo->setCurrentIndex(idx);
        applyDescriptor(m_descriptors.at(idx), initial);
    }
}

QJsonObject TransformField::request() const
{
    // 文本命中下拉项 -> 按 descriptor；否则按表达式。
    TransformKind kind = TransformKind::Builtin;
    QString id = currentDescriptorId();
    const QString text = m_combo->currentText();
    if (id.isEmpty())
    {
        id = QStringLiteral("expression");
        kind = TransformKind::Expression;
    }
    else
    {
        kind = m_appCtx.transforms().get(id).kind;
    }

    TransformRequest req;
    req.descriptorId = id;
    req.kind = kind;
    if (kind == TransformKind::Expression)
        req.expression = m_expression->toPlainText();
    else if (kind == TransformKind::Lua)
    {
        if (!m_lastLuaRequest.isEmpty())
        {
            const TransformRequest saved = TransformRequest::fromJson(m_lastLuaRequest);
            req.luaSource = saved.luaSource;
            req.scriptId = saved.scriptId;
            req.descriptorId = saved.descriptorId.isEmpty() ? id : saved.descriptorId;
        }
    }
    else
    {
        const auto widgets = m_paramHost->findChildren<QWidget *>();
        for (QWidget *w : widgets)
        {
            const QString key = QtFieldRenderer::keyOf(w);
            if (!key.isEmpty())
                req.params.insert(key, QJsonValue::fromVariant(QtFieldRenderer::valueOf(w)));
        }
    }
    return req.toJson();
}

void TransformField::onEditLua()
{
    if (!m_appCtx.interactions().hasTransformScriptOps())
        return;
    TransformScriptDialog editor(m_appCtx, this);
    const TransformRequest cur = TransformRequest::fromJson(m_lastLuaRequest);
    editor.loadScript(cur.scriptId, cur.luaSource, cur.descriptorId);
    if (editor.exec() != QDialog::Accepted)
        return;
    m_lastLuaRequest = editor.savedRequest();
    refreshScripts();
    const QString newId = TransformRequest::fromJson(m_lastLuaRequest).descriptorId;
    const int idx = m_combo->findData(newId);
    if (idx >= 0)
    {
        m_updating = true;
        m_combo->setCurrentIndex(idx);
        m_updating = false;
    }
    Q_EMIT changed();
}

} // namespace nextpilot::plotscope::gui
