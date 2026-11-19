#include "TransformEditorDialog.h"

#include "AppContext.h"
#include "Dataset.h"
#include "Result.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::gui
{

TransformEditorDialog::TransformEditorDialog(AppContext &appCtx, uint32_t capability,
                                             const QString &contextTitle, QWidget *parent)
    : QDialog(parent), m_appCtx(appCtx), m_capability(capability)
{
    setWindowTitle(QStringLiteral("变换编辑器 · %1").arg(
        contextTitle.isEmpty() ? capabilityNames(capability) : contextTitle));
    resize(720, 760);

    auto *root = new QVBoxLayout(this);

    // --- 变换选择（按能力过滤，防选错）---
    root->addWidget(new QLabel(QStringLiteral("选择变换（仅显示支持 [%1] 的项）")
                                   .arg(capabilityNames(capability))));
    m_descriptorCombo = new QComboBox(this);
    m_descriptors = TransformRegistry::byCapability(static_cast<TransformCapability>(capability));
    for (const TransformDescriptor &d : m_descriptors)
    {
        m_descriptorCombo->addItem(QStringLiteral("%1  ·  %2  ·  [%3]")
                                       .arg(d.label, transformKindName(d.kind), capabilityNames(d.capabilities)),
                                   d.id);
    }
    root->addWidget(m_descriptorCombo);

    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setObjectName(QStringLiteral("textDim"));
    root->addWidget(m_description);

    // --- 参数区（按 descriptor.params 动态生成）---
    root->addWidget(new QLabel(QStringLiteral("参数")));
    m_paramHost = new QWidget(this);
    root->addWidget(m_paramHost);

    // --- 表达式 / Lua 代码 ---
    auto *exprLabel = new QLabel(QStringLiteral("数学表达式（x=值/时间；如 5*x+10/sin(x)）"), this);
    root->addWidget(exprLabel);
    m_expression = new QPlainTextEdit(this);
    m_expression->setMaximumHeight(70);
    root->addWidget(m_expression);

    auto *luaLabel = new QLabel(QStringLiteral("Lua 代码（function transform(x, ctx) return y end，或单表达式）"), this);
    root->addWidget(luaLabel);
    m_lua = new QPlainTextEdit(this);
    m_lua->setMinimumHeight(120);
    root->addWidget(m_lua);

    // --- 在线调试：单点（live）---
    auto *singleBox = new QGroupBox(QStringLiteral("在线调试 · 单点（live）"), this);
    auto *singleLay = new QFormLayout(singleBox);
    m_singleX = new QLineEdit(singleBox);
    m_singleX->setText(QStringLiteral("1"));
    m_singleT = new QLineEdit(singleBox);
    m_singleT->setText(QStringLiteral("0"));
    singleLay->addRow(QStringLiteral("x"), m_singleX);
    singleLay->addRow(QStringLiteral("t"), m_singleT);
    auto *dbgSingle = new QPushButton(QStringLiteral("求值"), singleBox);
    singleLay->addRow(dbgSingle);
    root->addWidget(singleBox);

    // --- 在线调试：批量（offline）---
    auto *batchBox = new QGroupBox(QStringLiteral("在线调试 · 批量（offline）"), this);
    auto *batchLay = new QVBoxLayout(batchBox);
    m_batch = new QPlainTextEdit(batchBox);
    m_batch->setPlaceholderText(QStringLiteral("每行一个数值；或点“取当前活动通道”"));
    m_batch->setMinimumHeight(80);
    batchLay->addWidget(m_batch);
    auto *batchBtns = new QHBoxLayout;
    auto *dbgBatch = new QPushButton(QStringLiteral("批量求值"), batchBox);
    auto *fillBtn = new QPushButton(QStringLiteral("取当前活动通道"), batchBox);
    batchBtns->addWidget(dbgBatch);
    batchBtns->addWidget(fillBtn);
    batchLay->addLayout(batchBtns);
    root->addWidget(batchBox);

    m_result = new QLabel(QStringLiteral("结果：—"), this);
    m_result->setWordWrap(true);
    root->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(m_descriptorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TransformEditorDialog::onDescriptorChanged);
    connect(dbgSingle, &QPushButton::clicked, this, &TransformEditorDialog::onDebugSingle);
    connect(dbgBatch, &QPushButton::clicked, this, &TransformEditorDialog::onDebugBatch);
    connect(fillBtn, &QPushButton::clicked, this, &TransformEditorDialog::onFillFromActiveChannel);
    connect(buttons, &QDialogButtonBox::accepted, this, &TransformEditorDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (!m_descriptors.isEmpty())
        onDescriptorChanged(0);
}

TransformEditorDialog::~TransformEditorDialog() = default;

void TransformEditorDialog::applyDescriptor(const TransformDescriptor &d, const QJsonObject &initial)
{
    m_description->setText(QStringLiteral("%1\n示例：%2\n参数：%3")
                               .arg(d.description, d.example.isEmpty() ? QStringLiteral("—") : d.example,
                                    d.params.isEmpty() ? QStringLiteral("无")
                                                       : QStringLiteral("见下方字段")));
    // 初始请求回填。
    const TransformRequest init = TransformRequest::fromJson(initial);
    const QString expr = init.expression.isEmpty()
        ? (d.kind == TransformKind::Expression ? d.example : QString()) : init.expression;
    const QString lua = init.luaSource.isEmpty()
        ? (d.kind == TransformKind::Lua ? d.example : QString()) : init.luaSource;
    m_expression->setPlainText(expr);
    m_lua->setPlainText(lua);
    m_expression->setEnabled(d.kind == TransformKind::Expression);
    m_lua->setEnabled(d.kind == TransformKind::Lua);
    rebuildParamWidgets();
    // 回填参数值。
    if (d.kind == TransformKind::Builtin || d.kind == TransformKind::Lua)
    {
        auto *form = qobject_cast<QFormLayout *>(m_paramHost->layout());
        if (form)
        {
            for (int i = 0; i < form->rowCount(); ++i)
            {
                QLayoutItem *field = form->itemAt(i, QFormLayout::FieldRole);
                if (!field || !field->widget())
                    continue;
                const QString key = field->widget()->property("paramKey").toString();
                const QVariant v = init.params.value(key);
                if (auto *le = qobject_cast<QLineEdit *>(field->widget()))
                    le->setText(v.isValid() ? v.toString() : d.params.at(i).defaultValue);
                else if (auto *cb = qobject_cast<QCheckBox *>(field->widget()))
                    cb->setChecked(v.isValid() ? v.toBool() : d.params.at(i).defaultValue == QStringLiteral("true"));
            }
        }
    }
}

void TransformEditorDialog::rebuildParamWidgets()
{
    // 清空旧布局与其中的控件。
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

    const int idx = m_descriptorCombo->currentIndex();
    if (idx < 0 || idx >= m_descriptors.size())
        return;
    const TransformDescriptor &d = m_descriptors.at(idx);
    for (const TransformParam &p : d.params)
    {
        if (p.type == QStringLiteral("bool"))
        {
            auto *cb = new QCheckBox(m_paramHost);
            cb->setProperty("paramKey", p.key);
            cb->setToolTip(p.description);
            form->addRow(p.label, cb);
        }
        else
        {
            auto *le = new QLineEdit(m_paramHost);
            le->setProperty("paramKey", p.key);
            le->setPlaceholderText(p.description);
            le->setText(p.defaultValue);
            form->addRow(p.label, le);
        }
    }
    if (d.params.isEmpty())
        form->addRow(new QLabel(QStringLiteral("（该变换无参数）"), m_paramHost));
}

void TransformEditorDialog::onDescriptorChanged(int index)
{
    if (index < 0 || index >= m_descriptors.size())
        return;
    // 清空回填让它用 descriptor 默认；用户可再填。
    applyDescriptor(m_descriptors.at(index), QJsonObject());
}

QJsonObject TransformEditorDialog::collectRequest() const
{
    const int idx = m_descriptorCombo->currentIndex();
    if (idx < 0 || idx >= m_descriptors.size())
        return QJsonObject();
    const TransformDescriptor &d = m_descriptors.at(idx);
    QJsonObject params;
    if (auto *form = qobject_cast<QFormLayout *>(m_paramHost->layout()))
    {
        for (int i = 0; i < form->rowCount(); ++i)
        {
            QLayoutItem *field = form->itemAt(i, QFormLayout::FieldRole);
            if (!field || !field->widget())
                continue;
            const QString key = field->widget()->property("paramKey").toString();
            if (key.isEmpty())
                continue;
            if (auto *le = qobject_cast<QLineEdit *>(field->widget()))
                params.insert(key, le->text());
            else if (auto *cb = qobject_cast<QCheckBox *>(field->widget()))
                params.insert(key, cb->isChecked());
        }
    }
    return transformRequestJson(d.id, d.kind,
                                d.kind == TransformKind::Expression ? m_expression->toPlainText() : QString(),
                                d.kind == TransformKind::Lua ? m_lua->toPlainText() : QString(),
                                params);
}

// 用小函数构造请求 JSON（避免依赖 script 模块的 makeRequest）。
QJsonObject TransformEditorDialog::transformRequestJson(const QString &id, TransformKind kind,
                                                        const QString &expr, const QString &lua,
                                                        const QJsonObject &params) const
{
    TransformRequest req;
    req.descriptorId = id;
    req.kind = kind;
    req.expression = expr;
    req.luaSource = lua;
    req.params = params;
    return req.toJson();
}

void TransformEditorDialog::onDebugSingle()
{
    const QJsonObject req = collectRequest();
    if (req.isEmpty())
        return;
    const double x = m_singleX->text().toDouble();
    const double t = m_singleT->text().toDouble();
    Result<QVector<double>> out{QVector<double>()};
    if (!m_appCtx.hasTransformEvaluator())
    {
        m_lastResult = QStringLiteral("求值失败：变换求值器未注册");
        m_result->setText(QStringLiteral("结果：%1").arg(m_lastResult));
        return;
    }
    m_appCtx.evalTransform(req, m_capability, QVector<double>{x}, QVector<double>{x}, out);
    if (!out)
    {
        m_lastResult = out.error().message;
        m_result->setText(QStringLiteral("结果：失败 — %1").arg(m_lastResult));
        return;
    }
    const QVector<double> &vals = out.value();
    m_lastResult = vals.isEmpty() ? QStringLiteral("空结果")
                                  : QString::number(vals.first(), 'g', 10);
    m_result->setText(QStringLiteral("结果：f(%1, t=%2) = %3")
                          .arg(x).arg(t, 0, 'g', 4).arg(m_lastResult));
}

void TransformEditorDialog::onDebugBatch()
{
    const QJsonObject req = collectRequest();
    if (req.isEmpty())
        return;
    QVector<double> v;
    const QString text = m_batch->toPlainText();
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
    {
        bool ok = false;
        const double x = line.trimmed().toDouble(&ok);
        if (ok)
            v.append(x);
    }
    if (v.isEmpty())
    {
        m_lastResult = QStringLiteral("批量输入为空或没有合法数值");
        m_result->setText(QStringLiteral("结果：%1").arg(m_lastResult));
        return;
    }
    QVector<double> t(v.size(), 0.0);
    Result<QVector<double>> out{QVector<double>()};
    m_appCtx.evalTransform(req, m_capability, t, v, out);
    if (!out)
    {
        m_lastResult = out.error().message;
        m_result->setText(QStringLiteral("结果：失败 — %1").arg(m_lastResult));
        return;
    }
    const QVector<double> &vals = out.value();
    QStringList preview;
    for (int i = 0; i < qMin(8, vals.size()); ++i)
        preview << QString::number(vals.at(i), 'g', 8);
    m_lastResult = QStringLiteral("%1 点，前几个：%2").arg(vals.size()).arg(preview.join(QStringLiteral(", ")));
    m_result->setText(QStringLiteral("结果：%1").arg(m_lastResult));
}

void TransformEditorDialog::onFillFromActiveChannel()
{
    const int dsId = m_appCtx.activeDatasetId();
    const Dataset *ds = m_appCtx.data().get(dsId);
    if (!ds || ds->tableCount() <= 0)
    {
        QMessageBox::information(this, QStringLiteral("取活动通道"), QStringLiteral("当前没有活动数据集"));
        return;
    }
    const Table *t = ds->table(0);
    if (!t || t->columns.isEmpty())
        return;
    auto ch = ds->channel(t->name, t->columns.first().name);
    if (!ch)
        return;
    QStringList lines;
    const int take = qMin(2000, ch->size());
    for (int i = 0; i < take; ++i)
        lines << QString::number(ch->value.value(i), 'g', 10);
    m_batch->setPlainText(lines.join(QStringLiteral("\n")));
}

void TransformEditorDialog::onAccept()
{
    accept();
}

QJsonObject TransformEditorDialog::currentRequest() const
{
    return collectRequest();
}

int TransformEditorDialog::descriptorCount() const
{
    return m_descriptors.size();
}

QString TransformEditorDialog::lastResult() const
{
    return m_lastResult;
}

QJsonObject TransformEditorDialog::run(QWidget *parent, AppContext &appCtx, uint32_t capability,
                                       const QString &contextTitle, const QJsonObject &initial)
{
    TransformEditorDialog dlg(appCtx, capability, contextTitle, parent);
    if (!initial.isEmpty() && dlg.m_descriptorCombo)
    {
        const QString id = TransformRequest::fromJson(initial).descriptorId;
        const int idx = dlg.m_descriptorCombo->findData(id);
        if (idx >= 0)
        {
            dlg.m_descriptorCombo->setCurrentIndex(idx);
            dlg.applyDescriptor(dlg.m_descriptors.at(idx), initial);
        }
    }
    if (dlg.exec() != QDialog::Accepted)
        return QJsonObject();
    return dlg.currentRequest();
}

} // namespace nextpilot::plotscope::gui
