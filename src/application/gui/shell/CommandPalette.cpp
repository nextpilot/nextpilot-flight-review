#include "CommandPalette.h"

#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace nextpilot::plotscope::gui
{

namespace
{
constexpr int kIdRole = Qt::UserRole;
constexpr const char *kPaletteStyle = R"(
QWidget#commandPalette {
    background: palette(window);
    border: 1px solid palette(mid);
}
QLabel#commandPalette.title {
    color: palette(text);
    font-size: 12px;
    font-weight: bold;
    padding: 2px 4px;
}
QLineEdit#commandPalette.input {
    border: 1px solid palette(mid);
    border-radius: 3px;
    padding: 5px 7px;
    background: palette(base);
    color: palette(text);
    font-size: 13px;
}
QTreeWidget#commandPalette.list {
    border: 0;
    background: palette(base);
    color: palette(text);
    font-size: 12px;
    outline: 0;
}
QTreeWidget#commandPalette.list::item {
    padding: 4px 2px;
}
QTreeWidget#commandPalette.list::item:selected {
    background: palette(highlight);
    color: palette(highlighted-text);
}
)";
} // namespace

CommandPalette::CommandPalette(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("commandPalette"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromLatin1(kPaletteStyle));
    setFocusPolicy(Qt::StrongFocus);
    hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(5);

    m_titleLabel = new QLabel(QStringLiteral("命令面板"), this);
    m_titleLabel->setObjectName(QStringLiteral("commandPalette.title"));
    layout->addWidget(m_titleLabel);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("commandPalette.input"));
    m_input->setPlaceholderText(QStringLiteral("输入命令名或关键字…"));
    m_input->installEventFilter(this);
    layout->addWidget(m_input);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("commandPalette.list"));
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({QStringLiteral("命令"), QStringLiteral("快捷键"),
                             QStringLiteral("说明")});
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);

    connect(m_input, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
        activateCurrent();
    });

    if (parent)
        parent->installEventFilter(this);
}

void CommandPalette::setActivationHandler(std::function<void(const QString &)> handler)
{
    m_activationHandler = std::move(handler);
}

void CommandPalette::openCommands(const QList<Item> &items, const QString &title)
{
    m_mode = QStringLiteral("commands");
    m_items = items;
    if (m_titleLabel)
        m_titleLabel->setText(title.isEmpty() ? QStringLiteral("命令面板") : title);
    m_input->setPlaceholderText(QStringLiteral("输入命令名或关键字…"));
    m_input->clear();
    rebuild();

    const QRect area = parentWidget() ? parentWidget()->rect() : QRect(0, 0, 1200, 800);
    const int width = qMin(760, qMax(420, area.width() - 120));
    const int height = qMin(430, qMax(260, area.height() - 160));
    setGeometry(area.x() + (area.width() - width) / 2, area.y() + 68, width, height);
    show();
    raise();
    m_input->setFocus();
}

void CommandPalette::openQuickSwitch(const QList<Item> &items)
{
    openCommands(items, QStringLiteral("快速切换"));
    m_mode = QStringLiteral("quick");
    if (m_input)
        m_input->setPlaceholderText(QStringLiteral("快速切换编辑器、面板或 Activity…"));
}

void CommandPalette::closePalette()
{
    hide();
    if (parentWidget())
        parentWidget()->setFocus();
}

QString CommandPalette::query() const
{
    return m_input ? m_input->text() : QString();
}

void CommandPalette::setQuery(const QString &query)
{
    if (m_input)
        m_input->setText(query);
}

QStringList CommandPalette::visibleIds() const
{
    QStringList out;
    if (!m_list)
        return out;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        out << m_list->topLevelItem(i)->data(0, kIdRole).toString();
    return out;
}

int CommandPalette::visibleCount() const
{
    return m_list ? m_list->topLevelItemCount() : 0;
}

bool CommandPalette::activateCurrent()
{
    if (!m_list)
        return false;
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item)
        return false;
    const QString id = item->data(0, kIdRole).toString();
    for (const Item &candidate : m_items)
    {
        if (candidate.id != id)
            continue;
        if (!candidate.reason.isEmpty())
            return false; // 灰显项不执行，保留面板展示原因
        activateItem(candidate);
        return true;
    }
    return false;
}

void CommandPalette::rebuild()
{
    if (!m_list || !m_input)
        return;
    m_list->clear();

    const QString queryText = m_input->text();
    QList<Item> filtered;
    for (const Item &item : m_items)
        if (matches(item, queryText))
            filtered.append(item);

    std::stable_sort(filtered.begin(), filtered.end(), [this](const Item &a, const Item &b) {
        const int ai = m_mru.indexOf(a.id);
        const int bi = m_mru.indexOf(b.id);
        if (ai < 0 && bi < 0)
            return false;
        if (ai < 0)
            return false;
        if (bi < 0)
            return true;
        return ai < bi;
    });

    for (const Item &item : filtered)
    {
        QString title = item.title;
        if (!item.category.isEmpty())
            title = QStringLiteral("%1 · %2").arg(item.category, item.title);
        auto *row = new QTreeWidgetItem(m_list, {title, item.shortcut, item.reason});
        row->setData(0, kIdRole, item.id);
        if (!item.reason.isEmpty())
        {
            const QBrush grey = palette().brush(QPalette::Disabled, QPalette::Text);
            row->setForeground(0, grey);
            row->setForeground(1, grey);
            row->setForeground(2, grey);
            row->setToolTip(0, item.reason);
        }
    }

    if (m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0));
}

bool CommandPalette::matches(const Item &item, const QString &query) const
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return true;
    const QString haystack = QStringLiteral("%1 %2 %3 %4")
                                 .arg(item.title, item.id, item.category, item.keywords)
                                 .toLower();
    int pos = 0;
    for (const QChar ch : q)
    {
        pos = haystack.indexOf(ch, pos);
        if (pos < 0)
            return false;
        ++pos;
    }
    return true;
}

void CommandPalette::activateItem(const Item &item)
{
    if (item.action)
        item.action();
    m_mru.removeAll(item.id);
    m_mru.prepend(item.id);
    if (m_mru.size() > 24)
        m_mru = m_mru.mid(0, 24);
    if (m_activationHandler)
        m_activationHandler(item.id);
    closePalette();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)
        {
            m_list->setFocus();
            const int count = m_list->topLevelItemCount();
            if (count <= 0)
                return true;
            int row = m_list->currentIndex().row();
            if (row < 0)
                row = key->key() == Qt::Key_Down ? -1 : count;
            row = key->key() == Qt::Key_Down ? qMin(row + 1, count - 1) : qMax(row - 1, 0);
            m_list->setCurrentItem(m_list->topLevelItem(row));
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            activateCurrent();
            return true;
        }
        if (key->key() == Qt::Key_Escape)
        {
            closePalette();
            return true;
        }
    }

    if (watched == m_list && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            activateCurrent();
            return true;
        }
        if (key->key() == Qt::Key_Escape)
        {
            closePalette();
            return true;
        }
    }

    if (watched == parentWidget() && event->type() == QEvent::MouseButtonPress && isVisible())
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!geometry().contains(mouse->pos()))
            closePalette();
    }

    return QWidget::eventFilter(watched, event);
}

bool CommandPalette::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate)
        closePalette();
    return QWidget::event(event);
}

} // namespace nextpilot::plotscope::gui
