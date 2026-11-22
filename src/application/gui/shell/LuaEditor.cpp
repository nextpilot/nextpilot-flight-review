#include "LuaEditor.h"

#include "AppContext.h"
#include "Dataset.h"
#include "Result.h"
#include "Transform.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui
{

LuaEditor::LuaEditor(AppContext &appCtx, QWidget *parent)
    : QDialog(parent), m_appCtx(appCtx)
{
    setWindowTitle(QStringLiteral("Lua 编辑器"));
    resize(720, 680);

    auto *root = new QVBoxLayout(this);

    auto *meta = new QFormLayout;
    m_name = new QComboBox(this);
    m_name->setEditable(true);
    m_label = new QLineEdit(this);
    meta->addRow(QStringLiteral("脚本名"), m_name);
    meta->addRow(QStringLiteral("说明"), m_label);
    root->addLayout(meta);

    root->addWidget(new QLabel(QStringLiteral("代码（function transform(x, ctx) return y end）"), this));
    m_code = new QPlainTextEdit(this);
    m_code->setMinimumHeight(180);
    m_code->setPlainText(QStringLiteral("function transform(x, ctx)\n  return x\nend"));
    root->addWidget(m_code);

    auto *single = new QGroupBox(QStringLiteral("单点调试"), this);
    auto *singleLay = new QFormLayout(single);
    m_singleX = new QLineEdit(QStringLiteral("1"), single);
    m_singleT = new QLineEdit(QStringLiteral("0"), single);
    singleLay->addRow(QStringLiteral("x"), m_singleX);
    singleLay->addRow(QStringLiteral("t"), m_singleT);
    auto *dbgSingle = new QPushButton(QStringLiteral("求值"), single);
    singleLay->addRow(dbgSingle);
    root->addWidget(single);

    auto *batch = new QGroupBox(QStringLiteral("批量调试"), this);
    auto *batchLay = new QVBoxLayout(batch);
    m_batch = new QPlainTextEdit(batch);
    m_batch->setPlaceholderText(QStringLiteral("每行一个数值"));
    m_batch->setMinimumHeight(70);
    batchLay->addWidget(m_batch);
    auto *batchBtns = new QHBoxLayout;
    auto *dbgBatch = new QPushButton(QStringLiteral("批量求值"), batch);
    auto *fillBtn = new QPushButton(QStringLiteral("取当前活动通道"), batch);
    batchBtns->addWidget(dbgBatch);
    batchBtns->addWidget(fillBtn);
    batchLay->addLayout(batchBtns);
    root->addWidget(batch);

    m_result = new QLabel(QStringLiteral("结果：—"), this);
    m_result->setWordWrap(true);
    root->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(this);
    auto *saveBtn = buttons->addButton(QStringLiteral("保存脚本"), QDialogButtonBox::ApplyRole);
    auto *closeBtn = buttons->addButton(QDialogButtonBox::Close);
    root->addWidget(buttons);

    connect(saveBtn, &QPushButton::clicked, this, &LuaEditor::onSave);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(dbgSingle, &QPushButton::clicked, this, &LuaEditor::onDebugSingle);
    connect(dbgBatch, &QPushButton::clicked, this, &LuaEditor::onDebugBatch);
    connect(fillBtn, &QPushButton::clicked, this, &LuaEditor::onFillFromActiveChannel);
}

LuaEditor::~LuaEditor() = default;

void LuaEditor::loadScript(const QString &scriptId, const QString &source, const QString &name)
{
    m_loadedScriptId = scriptId;
    if (!scriptId.isEmpty())
        m_name->setEditText(scriptId);
    else if (!name.isEmpty() && name.startsWith(QStringLiteral("lua.")))
        m_name->setEditText(name.mid(4));
    if (!source.isEmpty())
        m_code->setPlainText(source);
}

QJsonObject LuaEditor::currentRequest() const
{
    TransformRequest req;
    req.descriptorId = QStringLiteral("lua.inline");
    req.kind = TransformKind::Lua;
    req.luaSource = m_code->toPlainText();
    return req.toJson();
}

void LuaEditor::showResult(const QString &text)
{
    m_result->setText(QStringLiteral("结果：%1").arg(text));
}

void LuaEditor::onDebugSingle()
{
    if (!m_appCtx.interactions().hasTransformEvaluator())
    {
        showResult(QStringLiteral("求值器未注册"));
        return;
    }
    const double x = m_singleX->text().toDouble();
    const double t = m_singleT->text().toDouble();
    Result<QVector<double>> out{QVector<double>()};
    m_appCtx.interactions().evalTransform(currentRequest(), CapDerived, QVector<double>{x}, QVector<double>{x}, out);
    if (!out)
    {
        showResult(QStringLiteral("失败 — %1").arg(out.error().message));
        return;
    }
    const QVector<double> &vals = out.value();
    showResult(vals.isEmpty() ? QStringLiteral("空结果")
                              : QString::number(vals.first(), 'g', 10));
}

void LuaEditor::onDebugBatch()
{
    if (!m_appCtx.interactions().hasTransformEvaluator())
    {
        showResult(QStringLiteral("求值器未注册"));
        return;
    }
    QVector<double> v;
    for (const QString &line : m_batch->toPlainText().split('\n', Qt::SkipEmptyParts))
    {
        bool ok = false;
        const double x = line.trimmed().toDouble(&ok);
        if (ok)
            v.append(x);
    }
    if (v.isEmpty())
    {
        showResult(QStringLiteral("批量输入为空或没有合法数值"));
        return;
    }
    QVector<double> t(v.size(), 0.0);
    Result<QVector<double>> out{QVector<double>()};
    m_appCtx.interactions().evalTransform(currentRequest(), CapDerived, t, v, out);
    if (!out)
    {
        showResult(QStringLiteral("失败 — %1").arg(out.error().message));
        return;
    }
    const QVector<double> &vals = out.value();
    QStringList preview;
    for (int i = 0; i < qMin(8, vals.size()); ++i)
        preview << QString::number(vals.at(i), 'g', 8);
    showResult(QStringLiteral("%1 点，前几个：%2").arg(vals.size()).arg(preview.join(QStringLiteral(", "))));
}

void LuaEditor::onFillFromActiveChannel()
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

void LuaEditor::onSave()
{
    if (!m_appCtx.interactions().hasTransformScriptOps())
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("脚本库能力未注册"));
        return;
    }
    const QString name = m_name->currentText().trimmed();
    const QByteArray source = m_code->toPlainText().toUtf8();
    const TransformScriptOps &ops = m_appCtx.interactions().transformScriptOps();
    const Result<TransformDescriptor> r = ops.save(name, source);
    if (!r)
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), r.error().message);
        return;
    }

    TransformRequest req;
    req.descriptorId = r.value().id;
    req.kind = TransformKind::Lua;
    req.scriptId = name;
    req.luaSource = source;
    m_savedRequest = req.toJson();
    accept();
}

QJsonObject LuaEditor::savedRequest() const
{
    return m_savedRequest;
}

} // namespace nextpilot::plotscope::gui
