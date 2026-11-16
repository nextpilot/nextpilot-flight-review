#pragma once

#include "ThemeManager.h"

#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

namespace ps::ui
{
// 图标图库：框里一排缩略图 + 框内右侧展开箭头，箭头弹出全部分类。
// 数据 / 绘图两个 ribbon 共用这一套外观，样式只在这里维护，免得两边各写一份还长得不一样。
class RibbonGallery : public QFrame
{
public:
    explicit RibbonGallery(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("rbGallery"));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // 全部取主题 token，深/浅主题下都跟面板同色，不用再维护两张色表
        setStyleSheet(QStringLiteral(
            "QFrame#rbGallery{background:%1;border:1px solid %2;border-radius:3px;}"
            "QToolButton#rbGalleryTile{background:transparent;border:0;padding:0;}"
            "QToolButton#rbGalleryTile:hover{background:%3;border:1px solid %2;border-radius:2px;}"
            "QToolButton#rbGalleryMore{background:transparent;border:0;border-left:1px solid %2;"
            "color:%4;font-size:10px;padding:0;}"
            "QToolButton#rbGalleryMore:hover{background:%3;color:%5;}")
                          .arg(ThemeManager::color(QStringLiteral("canvasBg")).name(),
                               ThemeManager::color(QStringLiteral("border")).name(),
                               ThemeManager::color(QStringLiteral("hover")).name(),
                               ThemeManager::color(QStringLiteral("textDim")).name(),
                               ThemeManager::color(QStringLiteral("accent")).name()));

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(4, 3, 3, 3);
        lay->setSpacing(2);
        m_row = new QHBoxLayout;
        m_row->setContentsMargins(0, 0, 0, 0);
        m_row->setSpacing(2);
        lay->addLayout(m_row);
        m_more = new QToolButton(this);
        m_more->setObjectName(QStringLiteral("rbGalleryMore"));
        m_more->setText(QStringLiteral("▾"));
        m_more->setToolTip(QStringLiteral("展开全部"));
        m_more->setCursor(Qt::PointingHandCursor);
        m_more->setFixedWidth(16);
        m_more->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        lay->addWidget(m_more);
        connect(m_more, &QToolButton::clicked, this, [this] { showPopup(); });
    }

    // 直接摆在外框里的一行图标
    void setPrimaryActions(const QList<QAction *> &acts)
    {
        while (QLayoutItem *it = m_row->takeAt(0))
        {
            if (QWidget *w = it->widget())
                w->deleteLater();
            delete it;
        }
        for (QAction *a : acts)
            if (a)
                m_row->addWidget(makeTile(a, QSize(56, 32), QSize(64, 40)));
        updateGeometry();
    }

    // 箭头弹出的完整图库，按分类分组
    void addGroup(const QString &title, const QList<QAction *> &acts)
    {
        if (!acts.isEmpty())
            m_groups.append(qMakePair(title, acts));
    }

    QSize sizeHint() const override
    {
        return QSize(m_row->sizeHint().width() + 16 + 9, 46);
    }

protected:
    QToolButton *makeTile(QAction *a, const QSize &icon, const QSize &tile)
    {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("rbGalleryTile"));
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        b->setIconSize(icon);
        b->setFixedSize(tile);
        b->setToolTip(a->text());
        return b;
    }

    void showPopup()
    {
        if (m_groups.isEmpty())
            return;
        if (!m_popup)
        {
            m_popup = new QFrame(this, Qt::Popup);
            m_popup->setObjectName(QStringLiteral("rbGalleryPopup"));
            m_popup->setStyleSheet(QStringLiteral(
                "QFrame#rbGalleryPopup{background:%1;border:1px solid %2;}"
                "QLabel{color:%3;font-size:11px;padding:2px 3px;}"
                "QToolButton{background:transparent;border:0;padding:0;}"
                "QToolButton:hover{background:%4;border-radius:2px;}")
                                       .arg(ThemeManager::color(QStringLiteral("panelAlt")).name(),
                                            ThemeManager::color(QStringLiteral("border")).name(),
                                            ThemeManager::color(QStringLiteral("textDim")).name(),
                                            ThemeManager::color(QStringLiteral("hover")).name()));
            auto *root = new QVBoxLayout(m_popup);
            root->setContentsMargins(8, 6, 8, 8);
            root->setSpacing(4);
            for (const auto &g : m_groups)
            {
                root->addWidget(new QLabel(g.first, m_popup));
                auto *grid = new QGridLayout;
                grid->setContentsMargins(0, 0, 0, 0);
                grid->setSpacing(3);
                for (int i = 0; i < g.second.size(); ++i)
                {
                    QAction *a = g.second.at(i);
                    auto *b = new QToolButton(m_popup);
                    b->setDefaultAction(a);
                    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
                    b->setAutoRaise(true);
                    b->setIconSize(QSize(64, 36));
                    b->setFixedSize(70, 44);
                    b->setToolTip(a->text());
                    connect(a, &QAction::triggered, m_popup, &QWidget::hide);
                    grid->addWidget(b, i / 6, i % 6);
                }
                root->addLayout(grid);
            }
        }
        m_popup->adjustSize();
        const QPoint anchor = mapToGlobal(QPoint(0, height()));
        const QRect avail = screen() ? screen()->availableGeometry() : QRect(anchor, m_popup->size());
        const int x = qBound(avail.left() + 8, anchor.x(), avail.right() - m_popup->width() - 8);
        int y = anchor.y() + 6;
        if (y + m_popup->height() > avail.bottom())
            y = mapToGlobal(QPoint(0, 0)).y() - m_popup->height() - 6;
        m_popup->move(x, y);
        m_popup->show();
    }

private:
    QHBoxLayout *m_row = nullptr;
    QToolButton *m_more = nullptr;
    QWidget *m_popup = nullptr;
    QList<QPair<QString, QList<QAction *>>> m_groups;
};
} // namespace ps::ui
