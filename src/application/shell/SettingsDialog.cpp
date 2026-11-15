#include "SettingsDialog.h"
#include "MainWindow.h"
#include "SettingsManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

using ps::components::SettingsContribution;

namespace
{
QWidget *buildGeneralPage(QCheckBox **restoreLayout)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    auto *group = new QGroupBox(QStringLiteral("启动"), page);
    auto *form = new QFormLayout(group);
    auto *restore = new QCheckBox(QStringLiteral("启动时恢复上次布局"), group);
    restore->setChecked(SettingsManager::instance().restoreLayout());
    form->addRow(restore);
    lay->addWidget(group);
    lay->addWidget(new QLabel(QStringLiteral("设置会写入 NextPilot / PlotScope。"), page));
    lay->addStretch(1);
    *restoreLayout = restore;
    return page;
}
} // namespace

SettingsDialog::SettingsDialog(MainWindow *mw)
    : QDialog(mw), m_mw(mw)
{
    setWindowTitle(QStringLiteral("设置"));
    resize(720, 460);

    auto *root = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setFixedWidth(160);
    m_list->setAlternatingRowColors(false);
    m_stack = new QStackedWidget(this);
    body->addWidget(m_list);
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);

    QCheckBox *restore = nullptr;
    QWidget *general = buildGeneralPage(&restore);
    addPage(QStringLiteral("通用"), QStringLiteral(":/icons/settings.svg"), general, [restore] {
        if (restore)
        {
            SettingsManager::instance().setRestoreLayout(restore->isChecked());
        }
    });

    QVector<SettingsContribution> pages = mw->extensions().registry().settings();
    std::sort(pages.begin(), pages.end(), [](const SettingsContribution &a, const SettingsContribution &b) {
        return a.order != b.order ? a.order < b.order : a.title < b.title;
    });
    for (const SettingsContribution &c : pages)
    {
        QWidget *page = c.create ? c.create(mw) : nullptr;
        if (!page)
            continue;
        addPage(c.title, c.icon, page, [c, page] { if (c.apply) c.apply(page); });
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        applyAll();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { applyAll(); });
    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void SettingsDialog::addPage(const QString &title, const QString &icon, QWidget *page,
                             std::function<void()> apply)
{
    if (!page)
        return;
    auto *item = new QListWidgetItem(title, m_list);
    if (!icon.isEmpty())
        item->setIcon(QIcon(icon));
    m_stack->addWidget(page);
    if (apply)
        m_appliers.append(std::move(apply));
}

void SettingsDialog::applyAll()
{
    for (const auto &apply : m_appliers)
        if (apply)
            apply();
}
