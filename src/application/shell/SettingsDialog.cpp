#include "SettingsDialog.h"
#include "MainWindow.h"
#include "SettingsManager.h"
#include "ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

using ps::components::SettingsContribution;

namespace
{
QWidget *buildGeneralPage(QCheckBox **restoreLayout, QComboBox **theme)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    auto *group = new QGroupBox(QStringLiteral("外观"), page);
    auto *form = new QFormLayout(group);
    auto *themeBox = new QComboBox(group);
    themeBox->addItem(QStringLiteral("深色"));
    themeBox->addItem(QStringLiteral("浅色"));
    themeBox->setCurrentIndex(ThemeManager::isDark() ? 0 : 1);
    form->addRow(QStringLiteral("主题"), themeBox);
    lay->addWidget(group);

    auto *startup = new QGroupBox(QStringLiteral("启动"), page);
    auto *startupForm = new QFormLayout(startup);
    auto *restore = new QCheckBox(QStringLiteral("启动时恢复上次布局"), startup);
    restore->setChecked(SettingsManager::instance().restoreLayout());
    startupForm->addRow(restore);
    lay->addWidget(startup);
    lay->addWidget(new QLabel(QStringLiteral("设置会写入 NextPilot / PlotScope。"), page));
    lay->addStretch(1);
    *restoreLayout = restore;
    *theme = themeBox;
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
    QComboBox *theme = nullptr;
    QWidget *general = buildGeneralPage(&restore, &theme);
    addPage(QStringLiteral("通用"), QStringLiteral(":/icons/settings.svg"), general, [this, restore, theme] {
        if (theme)
        {
            ThemeManager::setMode(theme->currentIndex() == 0 ? ThemeManager::Mode::Dark : ThemeManager::Mode::Light);
            m_mw->applyTheme();
            SettingsManager::instance().set(ps::settings::kTheme, ThemeManager::name());
            SettingsManager::instance().sync();
        }
        if (restore)
            SettingsManager::instance().setRestoreLayout(restore->isChecked());
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
