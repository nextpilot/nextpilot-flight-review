#include "RibbonBar.h"
#include "Intent.h"
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>



using namespace ng::core;

namespace ng::gui
{


namespace
{

// 分组容器：大按钮在上、小按钮列在下，底部一条标题。
// 纯 UI 组件，不含业务逻辑。
class GroupBox : public QWidget
{
public:
    GroupBox(const QString &label, QWidget *parent = nullptr)
        : QWidget(parent), m_label(label)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(8, 4, 8, 2);
        lay->setSpacing(2);

        m_big = new QToolButton(this);
        m_big->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_big->setMinimumSize(72, 48);
        lay->addWidget(m_big, 0, Qt::AlignHCenter);

        m_smallBox = new QWidget(this);
        m_smallLay = new QVBoxLayout(m_smallBox);
        m_smallLay->setContentsMargins(0, 0, 0, 0);
        m_smallLay->setSpacing(1);
        lay->addWidget(m_smallBox);

        auto *title = new QLabel(label, this);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet(QStringLiteral("color:#555; font-size:11px;"));
        lay->addWidget(title);
    }

    QToolButton *bigButton() const { return m_big; }
    QWidget *smallBox() const { return m_smallBox; }
    QVBoxLayout *smallLayout() const { return m_smallLay; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QColor(0xC0, 0xC0, 0xC0));
        // 右侧竖线：分组之间的视觉分隔
        p.drawLine(width() - 1, 6, width() - 1, height() - 18);
    }

private:
    QString m_label;
    QToolButton *m_big = nullptr;
    QWidget *m_smallBox = nullptr;
    QVBoxLayout *m_smallLay = nullptr;
};

} // namespace

RibbonBar::RibbonBar(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 页签行
    m_tabRow = new QWidget(this);
    m_tabLay = new QHBoxLayout(m_tabRow);
    m_tabLay->setContentsMargins(4, 2, 4, 2);
    m_tabLay->setSpacing(2);
    m_tabLay->addStretch(1);
    root->addWidget(m_tabRow);

    // 页面栈
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RibbonBar::rebuild(const UiSlotRegistry &slotReg)
{
    // 清空旧内容（重新构建是幂等的）。
    while (QWidget *w = m_stack->widget(0))
    {
        m_stack->removeWidget(w);
        delete w;
    }
    while (QLayoutItem *item = m_tabLay->takeAt(0))
    {
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }
    m_tabLay->addStretch(1);

    m_pages.clear();
    m_pageIndexByArea.clear();

    // 1) 页签
    const auto pages = slotReg.slotsOfKind(SlotKind::RibbonPage);
    for (const UiSlot &pageSlot : pages)
    {
        Page page;
        page.area = pageSlot.area;
        page.label = pageSlot.label;
        m_pages.append(page);
        m_pageIndexByArea.insert(pageSlot.area, m_pages.size() - 1);

        auto *tab = new QToolButton(m_tabRow);
        tab->setText(pageSlot.label);
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setProperty("pageArea", pageSlot.area);
        tab->setStyleSheet(QStringLiteral(
            "QToolButton{padding:4px 14px; border:1px solid transparent;}"
            "QToolButton:checked{background:#E8F0FB; border:1px solid #7EA6D8;}"));
        connect(tab, &QToolButton::clicked, this, &RibbonBar::onButtonClicked);
        m_tabLay->insertWidget(m_tabLay->count() - 1, tab);
    }
    if (m_pages.isEmpty())
        return;

    // 2) 每个页签一个页面容器，内含各分组
    for (int i = 0; i < m_pages.size(); ++i)
    {
        Page &page = m_pages[i];
        auto *pageWidget = new QWidget(m_stack);
        auto *pageLay = new QHBoxLayout(pageWidget);
        pageLay->setContentsMargins(6, 4, 6, 4);
        pageLay->setSpacing(10);

        for (const UiSlot &groupSlot : slotReg.childrenOf(page.area))
        {
            if (groupSlot.kind != SlotKind::RibbonGroup)
                continue;
            auto *box = new GroupBox(groupSlot.label, pageWidget);
            pageLay->addWidget(box);

            Group group;
            group.area = groupSlot.area;
            group.label = groupSlot.label;
            group.container = box;

            const auto actions = slotReg.actionsIn(groupSlot.area);
            for (int a = 0; a < actions.size(); ++a)
            {
                const UiAction &action = actions[a].action;
                auto *btn = new QToolButton(box);
                btn->setText(action.label);
                btn->setToolTip(action.label);
                btn->setProperty("actionId", action.id);
                btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
                btn->setMinimumWidth(84);
                connect(btn, &QToolButton::clicked, this, &RibbonBar::onButtonClicked);

                if (a == 0)
                {
                    box->bigButton()->setText(action.label);
                    box->bigButton()->setProperty("actionId", action.id);
                    connect(box->bigButton(), &QToolButton::clicked, this, &RibbonBar::onButtonClicked);
                    group.actionIds.append(action.id);
                    group.actionLabels.append(action.label);
                }
                else
                {
                    box->smallLayout()->addWidget(btn);
                    group.actionIds.append(action.id);
                    group.actionLabels.append(action.label);
                }
            }

            page.groups.append(group);
        }
        pageLay->addStretch(1);
        m_stack->addWidget(pageWidget);
    }

    m_currentPage = 0;
    m_stack->setCurrentIndex(0);
    // 首个页签选中
    if (QLayoutItem *item = m_tabLay->itemAt(0))
        if (auto *tab = qobject_cast<QToolButton *>(item->widget()))
            tab->setChecked(true);
}

void RibbonBar::onButtonClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;

    // 页签切换
    const QVariant pageArea = btn->property("pageArea");
    if (pageArea.isValid())
    {
        const auto it = m_pageIndexByArea.constFind(pageArea.toString());
        if (it != m_pageIndexByArea.constEnd())
        {
            m_currentPage = it.value();
            m_stack->setCurrentIndex(m_currentPage);
        }
        return;
    }

    // 动作点击
    const QVariant actionId = btn->property("actionId");
    if (actionId.isValid() && !actionId.toString().isEmpty())
        emit actionTriggered(actionId.toString());
}

int RibbonBar::pageCount() const
{
    return m_pages.size();
}

int RibbonBar::groupCount(const QString &pageArea) const
{
    const auto it = m_pageIndexByArea.constFind(pageArea);
    if (it == m_pageIndexByArea.constEnd())
        return 0;
    return m_pages[it.value()].groups.size();
}

int RibbonBar::actionCount(const QString &groupArea) const
{
    for (const Page &page : m_pages)
        for (const Group &group : page.groups)
            if (group.area == groupArea)
                return group.actionIds.size();
    return 0;
}

QStringList RibbonBar::pageAreas() const
{
    QStringList out;
    for (const Page &p : m_pages)
        out.append(p.area);
    return out;
}

void RibbonBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0xF5, 0xF5, 0xF5));
    // 页签行下边线
    p.setPen(QColor(0xD0, 0xD0, 0xD0));
    p.drawLine(0, m_tabRow->height(), width(), m_tabRow->height());
}

} // namespace ng::gui
