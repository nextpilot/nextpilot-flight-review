#include "PropertyManager.h"

#include "ThemeManager.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace ps::shell
{
namespace
{
class ThemePropDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        Q_UNUSED(index)
        option->palette.setColor(QPalette::Text, ThemeManager::color(QStringLiteral("text")));
    }
};

int leadingInt(const QString &text, int fallback)
{
    const QRegularExpression re(QStringLiteral("[-+]?\\d+"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return fallback;
    bool ok = false;
    const int v = m.captured(0).toInt(&ok);
    return ok ? v : fallback;
}

double leadingDouble(const QString &text, double fallback)
{
    const QRegularExpression re(QStringLiteral("[-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return fallback;
    bool ok = false;
    const double v = m.captured(0).toDouble(&ok);
    return ok ? v : fallback;
}
} // namespace

PropertyManager::PropertyManager(QWidget *parent, DockShow showDock)
    : QObject(parent), m_parent(parent), m_showDock(std::move(showDock)) {}

QWidget *PropertyManager::createTreeView()
{
    if (m_tree)
        return m_tree;
    auto *w = new QWidget(m_parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget(w);
    m_tree->setHeaderLabels({QStringLiteral("对象")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    lay->addWidget(m_tree);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (m_building || !item)
            return;
        const QString id = item->data(0, Qt::UserRole).toString();
        if (!id.isEmpty())
            showNode(id);
    });
    buildTree();
    return w;
}

QWidget *PropertyManager::createPanelView()
{
    if (m_table)
        return m_table->parentWidget();
    auto *w = new QWidget(m_parent);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *head = new QWidget(w);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(8, 4, 8, 4);
    m_title = new QLabel(QStringLiteral("未选择"), head);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setMinimumWidth(0);
    hl->addWidget(m_title, 1);
    lay->addWidget(head);

    m_table = new QTableWidget(0, 2, w);
    m_table->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::SelectedClicked);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setItemDelegate(new ThemePropDelegate(m_table));
    lay->addWidget(m_table, 1);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || !it || it->column() != 1)
            return;
        auto *key = m_table->item(it->row(), 0);
        if (key)
            pushEdit(key->text(), it->text());
    });
    return w;
}

void PropertyManager::registerNode(const PropertyNode &node)
{
    if (!m_nodes.contains(node.id))
        m_order.append(node.id);
    m_nodes.insert(node.id, node);
    if (!m_treeDirty)
    {
        m_treeDirty = true;
        QTimer::singleShot(0, this, [this] { m_treeDirty = false; buildTree(); });
    }
    ensureVisible();
}

void PropertyManager::removeNode(const QString &id)
{
    QStringList ids{id};
    for (const QString &other : m_nodes.keys())
        if (other.startsWith(id + QLatin1Char('.')))
            ids.append(other);
    for (const QString &key : ids)
    {
        m_nodes.remove(key);
        m_order.removeAll(key);
    }
    if (m_currentId.startsWith(id))
        m_currentId.clear();
    buildTree();
}

void PropertyManager::clearChildren(const QString &parentId)
{
    QString prefix = parentId + QLatin1Char('.');
    QStringList ids;
    for (const QString &key : m_nodes.keys())
        if (key.startsWith(prefix))
            ids.append(key);
    for (const QString &key : ids)
    {
        m_nodes.remove(key);
        m_order.removeAll(key);
    }
    if (m_currentId.startsWith(prefix))
        m_currentId.clear();
    buildTree();
}

void PropertyManager::updateNodeLabel(const QString &id, const QString &label)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end() || label.isEmpty())
        return;
    it->label = label;
    auto item = m_items.value(id);
    if (item)
        item->setText(0, label);
}

void PropertyManager::buildTree()
{
    if (!m_tree)
        return;
    QSignalBlocker blocker(m_tree);
    m_building = true;
    m_tree->clear();
    m_items.clear();
    for (const QString &key : m_order)
    {
        const PropertyNode &node = m_nodes.value(key);
        QTreeWidgetItem *parent = m_items.value(node.parentId);
        if (!node.parentId.isEmpty() && !parent)
            continue;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, node.label);
        item->setData(0, Qt::UserRole, node.id);
        if (!node.icon.isEmpty())
            item->setIcon(0, ThemeManager::icon(node.icon));
        m_items.insert(node.id, item);
    }
    m_tree->expandAll();
    m_building = false;
    if (!m_currentId.isEmpty())
        selectNode(m_currentId);
}

void PropertyManager::ensureVisible()
{
    if (!m_showDock || !hasNodes())
        return;
    QTimer::singleShot(0, this, [this] {
        if (!hasNodes() || m_docksShown)
            return;
        const bool propShown = m_showDock(QStringLiteral("dock.prop"), true);
        const bool treeShown = m_showDock(QStringLiteral("dock.tree"), false);
        if (propShown || treeShown)
            m_docksShown = true;
    });
}

void PropertyManager::selectNode(const QString &id)
{
    m_currentId = id;
    auto content = m_nodes.find(id);
    if (content == m_nodes.end() || !content->content)
        return;
    auto item = m_items.value(id);
    if (item && m_tree)
    {
        m_building = true;
        m_tree->setCurrentItem(item);
        m_building = false;
        item->setExpanded(true);
    }
    showNode(id);
}

void PropertyManager::showNode(const QString &id)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end() || !it->content)
        return;
    m_currentId = id;
    fillRows(it->content());
}

void PropertyManager::showContent(const PropertyContent &content)
{
    m_last = content;
    fillRows(content);
}

void PropertyManager::refreshCurrent()
{
    if (!m_currentId.isEmpty())
        showNode(m_currentId);
}

void PropertyManager::pushEdit(const QString &key, const QString &text)
{
    auto it = m_nodes.find(m_currentId);
    if (it == m_nodes.end() || !it->apply)
        return;
    it->apply(key, text);
    // 控件还在发信号，直接重建行会踩野指针，等事件循环回头再刷新。
    QTimer::singleShot(0, this, [this] { refreshCurrent(); });
}

void PropertyManager::fillRows(const PropertyContent &content)
{
    if (m_title)
    {
        const QString text = QStringLiteral("当前对象：%1").arg(content.title);
        m_title->setToolTip(text);
        m_title->setText(m_title->width() > 0
                             ? QFontMetrics(m_title->font()).elidedText(text, Qt::ElideRight, m_title->width())
                             : text);
    }
    if (!m_table)
        return;
    QSignalBlocker blocker(m_table);
    m_updating = true;
    m_table->setRowCount(content.rows.size());
    auto push = [this](const QString &key, const QString &value) { pushEdit(key, value); };
    auto centered = [](QWidget *inner, QWidget *parent) {
        auto *w = new QWidget(parent);
        w->setStyleSheet(QStringLiteral("QWidget{background:transparent;}"));
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(4, 0, 4, 0);
        l->addWidget(inner);
        l->addStretch(1);
        return w;
    };
    auto numberCombo = [&, push](const QString &key, const QString &text, int fallback) {
        QStringList presets;
        if (key == QStringLiteral("坐标轴字号"))
            presets = {QStringLiteral("9"),  QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12"),
                       QStringLiteral("13"), QStringLiteral("14"), QStringLiteral("16"), QStringLiteral("18")};
        else if (key == QStringLiteral("标记大小"))
            presets = {QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                       QStringLiteral("8"), QStringLiteral("10")};
        else
            presets = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")};
        auto *cb = new QComboBox(m_table);
        cb->setEditable(true);
        cb->setInsertPolicy(QComboBox::NoInsert);
        cb->addItems(presets);
        cb->setCurrentText(QString::number(leadingInt(text, fallback)));
        connect(cb->lineEdit(), &QLineEdit::editingFinished, this,
                [key, cb, push] { push(key, cb->currentText()); });
        connect(cb, QOverload<int>::of(&QComboBox::activated), this,
                [key, cb, push](int) { push(key, cb->currentText()); });
        return cb;
    };
    auto enumCombo = [&, push](const QString &key, const QString &text) {
        QStringList options;
        if (key == QStringLiteral("图例位置"))
            options = {QStringLiteral("左上"), QStringLiteral("右上"), QStringLiteral("左下"), QStringLiteral("右下")};
        else if (key == QStringLiteral("线型"))
            options = {QStringLiteral("实线"), QStringLiteral("虚线"), QStringLiteral("点线"), QStringLiteral("点划线")};
        else if (key == QStringLiteral("标记"))
            options = {QStringLiteral("无"), QStringLiteral("圆点"), QStringLiteral("方块"), QStringLiteral("菱形"),
                       QStringLiteral("三角"), QStringLiteral("十字"), QStringLiteral("叉号")};
        else
            options = {QStringLiteral("左 Y 轴"), QStringLiteral("右 Y 轴")};
        auto *cb = new QComboBox(m_table);
        cb->addItems(options);
        if (cb->currentText() != text)
            cb->setCurrentText(text);
        connect(cb, QOverload<int>::of(&QComboBox::activated), this,
                [key, cb, push](int) { push(key, cb->currentText()); });
        return cb;
    };
    auto colorButton = [&, push](const QString &key, const QString &text) {
        auto *btn = new QToolButton(m_table);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setStyleSheet(ThemeManager::mapStyleSheet(QStringLiteral(
            "QToolButton{color:#d5dde5;background:transparent;border:1px solid #2d3841;"
            "border-radius:2px;padding:0 6px;}QToolButton:hover{border-color:#f2b134;}")));
        btn->setCursor(Qt::PointingHandCursor);
        const bool themed = text.isEmpty() || text.contains(QStringLiteral("跟随"));
        QColor c = themed ? QColor(QStringLiteral("#3a4854")) : QColor(text);
        auto swatch = [](const QColor &col) {
            QPixmap pm(14, 14);
            pm.fill(col.isValid() ? col : QColor(QStringLiteral("#8b98a5")));
            return QIcon(pm);
        };
        btn->setIcon(swatch(c));
        btn->setText(themed ? QStringLiteral("跟随主题") : c.name());
        connect(btn, &QToolButton::clicked, this, [key, btn, swatch, push] {
            const QColor old = QColor(btn->text());
            const QColor sel = QColorDialog::getColor(old.isValid() ? old : Qt::white, btn, QStringLiteral("选择颜色"));
            if (sel.isValid())
                push(key, sel.name());
        });
        if (key == QStringLiteral("坐标轴颜色"))
        {
            btn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(btn, &QToolButton::customContextMenuRequested, this, [key, btn, push](const QPoint &p) {
                QMenu menu(btn);
                auto *reset = menu.addAction(QStringLiteral("跟随主题"));
                if (menu.exec(btn->mapToGlobal(p)) == reset)
                    push(key, QStringLiteral("跟随主题"));
            });
            btn->setToolTip(QStringLiteral("点击选择颜色，右键恢复跟随主题"));
        }
        return btn;
    };

    for (int i = 0; i < content.rows.size(); ++i)
    {
        const QString key = content.rows[i].first;
        const QString text = content.rows[i].second;
        const bool canEdit = content.editable.contains(key);
        auto *k = new QTableWidgetItem(key);
        k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, 0, k);

        auto *v = new QTableWidgetItem(text);
        v->setFlags(canEdit ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                            : Qt::ItemIsEnabled);

        QWidget *editor = nullptr;
        if (canEdit)
        {
            if (key == QStringLiteral("X 网格") || key == QStringLiteral("Y 网格") || key == QStringLiteral("次网格") ||
                key == QStringLiteral("边框") || key == QStringLiteral("FlipY") || key == QStringLiteral("可见") ||
                (content.kind == 1 && key == QStringLiteral("图例")))
            {
                auto *box = new QCheckBox(m_table);
                box->setChecked(text.contains(QStringLiteral("显示")) || text.contains(QStringLiteral("是")) ||
                                text == QStringLiteral("true"));
                connect(box, &QCheckBox::toggled, this, [key, push](bool on) {
                    push(key, on ? QStringLiteral("显示") : QStringLiteral("隐藏"));
                });
                editor = centered(box, m_table);
            }
            else if (key == QStringLiteral("坐标轴字号") || key == QStringLiteral("坐标轴线宽") ||
                     key == QStringLiteral("线宽") || key == QStringLiteral("标记大小"))
            {
                editor = numberCombo(key, text, key == QStringLiteral("坐标轴字号") ? 11 : 1);
            }
            else if (key == QStringLiteral("图例位置") || key == QStringLiteral("线型") ||
                     key == QStringLiteral("标记") || key == QStringLiteral("绑定轴"))
            {
                editor = enumCombo(key, text);
            }
            else if (key == QStringLiteral("坐标轴颜色") || key == QStringLiteral("颜色"))
            {
                editor = colorButton(key, text);
            }
            else if (key == QStringLiteral("时间偏移"))
            {
                auto *sp = new QDoubleSpinBox(m_table);
                sp->setDecimals(3);
                sp->setRange(-1e6, 1e6);
                sp->setSuffix(QStringLiteral(" s"));
                sp->setValue(text.split(QLatin1Char(' ')).first().toDouble());
                connect(sp, &QDoubleSpinBox::editingFinished, this,
                        [key, sp, push] { push(key, QString::number(sp->value(), 'f', 3)); });
                editor = sp;
            }
        }

        if (editor)
        {
            v->setFlags(Qt::ItemIsEnabled);
            v->setText(QString());
            v->setToolTip(text);
            m_table->setItem(i, 1, v);
            m_table->setCellWidget(i, 1, editor);
        }
        else
        {
            v->setFlags(canEdit ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                                : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            v->setToolTip(canEdit ? QStringLiteral("双击编辑，回车立即生效") : text);
            m_table->setItem(i, 1, v);
            m_table->removeCellWidget(i, 1);
        }
    }
    m_updating = false;
}
} // namespace ps::shell
