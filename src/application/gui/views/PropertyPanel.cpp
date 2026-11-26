#include "PropertyPanel.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "PanelFactoryHub.h"
#include "PropertySchema.h"
#include "Result.h"

#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
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

namespace
{
constexpr int kRoleObjectId = Qt::UserRole + 1;
constexpr int kRoleKey = Qt::UserRole + 2;
constexpr int kRoleEditable = Qt::UserRole + 3;
constexpr int kRoleKind = Qt::UserRole + 4;
} // namespace

PropertyPanel::PropertyPanel(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_executor(executor)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_header = new QLabel(QStringLiteral("属性"), this);
    m_header->setObjectName(QStringLiteral("sectionTitle"));
    m_header->setContentsMargins(8, 6, 8, 4);
    lay->addWidget(m_header);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    m_tree->setIndentation(14);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    lay->addWidget(m_tree, 1);

    m_empty = new QLabel(this);
    m_empty->setText(QStringLiteral("未选中对象\n在左侧数据集树点击一项，或点选画布上的曲线 / 坐标轴"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setObjectName(QStringLiteral("textDim"));
    lay->addWidget(m_empty, 1);

    connect(&m_appCtx, &AppContext::selectionChanged, this, [this](const QJsonObject &) {
        refreshFromSelection();
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, &PropertyPanel::onItemChanged);

    refreshFromSelection();
}

PropertyPanel::~PropertyPanel() = default;

void PropertyPanel::showEmpty(const QString &message)
{
    m_tree->hide();
    m_empty->show();
    m_empty->setText(message);
}

void PropertyPanel::refreshFromSelection()
{
    const Selection &sel = m_appCtx.selection();
    if (sel.empty())
    {
        m_header->setText(QStringLiteral("属性"));
        showEmpty(QStringLiteral("未选中对象\n在左侧数据集树点击一项，或点选画布上的曲线 / 坐标轴"));
        return;
    }

    QJsonObject schemaJson;
    if (!m_appCtx.interactions().queryPropertySchema(sel, schemaJson) || schemaJson.isEmpty())
    {
        m_header->setText(QStringLiteral("属性 · %1").arg(sel.label));
        showEmpty(QStringLiteral("「%1」暂无属性提供者").arg(sel.label));
        return;
    }
    rebuild(schemaJson);
}

void PropertyPanel::rebuild(const QJsonObject &schemaJson)
{
    PropertySchema schema = PropertySchema::fromJson(schemaJson);
    m_rebuilding = true;
    m_tree->clear();
    m_header->setText(QStringLiteral("属性 · %1").arg(schema.title.isEmpty() ? selLabelFallback() : schema.title));
    m_tree->show();
    m_empty->hide();

    // 先建所有对象节点，再挂父（parentId 可能排在后面）。
    QHash<QString, QTreeWidgetItem *> nodes;
    for (const PropObject &obj : schema.objects)
    {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, obj.label.isEmpty() ? obj.id : obj.label);
        item->setData(0, kRoleObjectId, obj.id);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        // 对象节点用粗体区分（属性行挂在它下面）。
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        nodes.insert(obj.id, item);
    }
    for (const PropObject &obj : schema.objects)
    {
        QTreeWidgetItem *parent = nodes.value(obj.parentId, nullptr);
        QTreeWidgetItem *item = nodes.value(obj.id, nullptr);
        if (!item)
            continue;
        if (parent && parent != item)
        {
            m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(item));
            parent->addChild(item);
        }
        // 属性行
        for (const PropRow &row : obj.rows)
        {
            auto *r = new QTreeWidgetItem(item);
            r->setText(0, row.label.isEmpty() ? row.key : row.label);
            QString shown = row.value;
            if (!row.unit.isEmpty())
                shown += QLatin1Char(' ') + row.unit;
            r->setText(1, shown);
            r->setData(1, kRoleObjectId, obj.id);
            r->setData(1, kRoleKey, row.key);
            r->setData(1, kRoleEditable, row.editable);
            r->setData(1, kRoleKind, static_cast<int>(row.kind));
            r->setToolTip(0, row.tooltip);
            r->setToolTip(1, row.tooltip);
            if (row.editable)
                r->setFlags(r->flags() | Qt::ItemIsEditable);
            else
                r->setFlags(r->flags() & ~Qt::ItemIsEditable);
        }
        item->setExpanded(true);
    }
    m_rebuilding = false;
}

QString PropertyPanel::selLabelFallback() const
{
    return m_appCtx.selection().label;
}

void PropertyPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_rebuilding || !item || column != 1)
        return;
    const bool editable = item->data(1, kRoleEditable).toBool();
    if (!editable)
        return;
    const QString objectId = item->data(1, kRoleObjectId).toString();
    const QString key = item->data(1, kRoleKey).toString();
    const QString value = item->text(1);
    commitEdit(objectId, key, value);
}

void PropertyPanel::commitEdit(const QString &objectId, const QString &key, const QString &value)
{
    // 按 selection.kind 路由到注册该类型时的写入器（provider/setter 成对）。
    // 写入器自己最懂如何把值写回持有者（曲线色/轴范围/协议字段…）。
    QString err;
    if (!m_appCtx.interactions().applyProperty(m_appCtx.selection(), objectId, key, value, &err))
    {
        // 兜底：没有专用写入器时走意图层（headless/脚本同路径）。
        Intent intent;
        intent.op = QStringLiteral("prop.set");
        intent.params[QStringLiteral("object")] = objectId;
        intent.params[QStringLiteral("key")] = key;
        intent.params[QStringLiteral("value")] = value;
        intent.params[QStringLiteral("source")] = selectionKindName(m_appCtx.selection().kind);
        m_executor.execute(intent);
    }
}

QString PropertyPanel::schemaTitle() const
{
    return m_header->text();
}

int PropertyPanel::objectCount() const
{
    return m_tree->topLevelItemCount();
}

int PropertyPanel::rowCount() const
{
    int n = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        n += m_tree->topLevelItem(i)->childCount();
    return n;
}

QString PropertyPanel::selectionKindText() const
{
    return selectionKindName(m_appCtx.selection().kind);
}

QWidget *createPropertyPanel(AppContext &appCtx, IIntentExecutor &exec, QWidget *parent)
{
    return new PropertyPanel(appCtx, exec, parent);
}

// 视图层自注册：模块只声明类型字符串 "property-panel"，外壳据此装配本面板。
// A2b 笔2b 第②步返工（复审必修 1）：plot.properties 不得由 core.workbench
// 声明（v3 §78"业务视图一律由模块自己声明"），property-panel 留工厂表、
// 笔 4 随 modules/plot/gui 归位时再迁（届时按表 A 改名 plot.properties）。
PS_REGISTER_PANEL_TI("property-panel", propertyPanel, createPropertyPanel, "属性", "property")

} // namespace nextpilot::plotscope::gui
