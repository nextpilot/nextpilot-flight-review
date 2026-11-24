#include "ThemeManager.h"
#include "ErrorCodes.h"
#include "ThemeTokens.h"

#include "SettingsStore.h"

#include <QApplication>
#include <QHash>
#include <QStyleFactory>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::gui
{

namespace
{
// 进程级当前主题：供 delegate 绘制时取色（见头文件说明）。
ThemeManager::Theme g_activeTheme = ThemeManager::Theme::Light;
} // namespace

namespace
{
// palette() 用到的内置 token 名（色表本体在 uischema::ThemeTokens，这里只留取色键名）。
const QString kWindow      = QStringLiteral("window");
const QString kPanel       = QStringLiteral("panel");
const QString kBase        = QStringLiteral("base");
const QString kText        = QStringLiteral("text");
const QString kTextDim     = QStringLiteral("textDim");
const QString kTextMuted   = QStringLiteral("textMuted");
const QString kBorder      = QStringLiteral("border");
const QString kAccent      = QStringLiteral("accent");
const QString kAccentText  = QStringLiteral("accentText");

// QSS 模板：颜色一律写成 $(token) 占位符，渲染时由 mapStyleSheet 替换。
// 遵守根 AGENTS §12.2：不给 QWidget 通配背景，只给顶层容器与明确命名的面板；
// 子控件默认透明，避免 QLabel 在 dock 选中行上盖出色块。
const QString kGlobalQss = QStringLiteral(R"(
QMainWindow, QDialog {
    background: $(window);
    color: $(text);
}
QWidget#dockPanel, QFrame#panel {
    background: $(panel);
    color: $(text);
}
QMenu, QToolTip {
    background: $(panel);
    color: $(text);
    border: 1px solid $(border);
}
QLabel, QCheckBox, QRadioButton, QGroupBox {
    color: $(text);
}
QLabel[secondary="true"], QLabel#textDim {
    color: $(textDim);
}
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: $(base);
    color: $(text);
    border: 1px solid $(border);
    border-radius: 3px;
    padding: 2px 4px;
    selection-background-color: $(selection);
    selection-color: $(selectionText);
}
QPushButton {
    background: $(panel);
    color: $(text);
    border: 1px solid $(border);
    border-radius: 3px;
    padding: 4px 10px;
}
QPushButton:hover { border-color: $(accent); }
QPushButton:checked, QPushButton:pressed {
    background: $(accent);
    color: $(accentText);
}
QTreeWidget, QTreeView, QTableView, QListView {
    background: $(base);
    color: $(text);
    border: 1px solid $(border);
    gridline-color: $(grid);
    selection-background-color: $(selection);
    selection-color: $(selectionText);
}
QHeaderView::section {
    background: $(panel);
    color: $(textDim);
    border: 0;
    border-bottom: 1px solid $(border);
    padding: 3px 6px;
}
QScrollBar:vertical, QScrollBar:horizontal {
    background: $(panel);
}
QScrollBar::handle {
    background: $(border);
    border-radius: 3px;
}
QScrollBar::handle:hover { background: $(textMuted); }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QStatusBar { color: $(textDim); }
QSplitter::handle { background: $(border); }
)");

// SARibbon 专属覆盖：设给 SARibbonBar 自身（它自绘，全局 QSS 压不住）。
// 用最小必要选择器（§12.4），颜色全来自 token（§12.1）。
const QString kRibbonQss = QStringLiteral(R"(
SARibbonBar { background: $(panel); }
SARibbonCategory { background: $(panel); }
SARibbonCategory > QWidget { background: transparent; }
SARibbonPanel { background: $(panel); border-right: 1px solid $(border); }
SARibbonPanelLabel { color: $(textDim); background: transparent; }
SARibbonToolButton { color: $(text); background: transparent; border: 1px solid transparent; }
SARibbonToolButton:hover { background: $(border); color: $(text); }
SARibbonToolButton:checked { background: $(accent); color: $(accentText); }
)");

// 停靠区/页签条样式。
//
// 为什么必须单独一套：ADS 自带 default_linux.css 用 palette(window) 画容器与
// 页签，active 页签是「window→light」渐变。ADS 的 palette 由 Fusion 主题给出，
// 与 token 化的面板底色不同源——于是画布组和侧栏组看起来是两种皮。更糟的是
// 旧代码给每个页签塞了内联 `background: transparent`，把 active 渐变也抹平了，
// 结果四组页签「底色不一致 + 看不出当前是哪一个」，页签高度还被压塌到与内容行
// 重叠。这里统一重写：容器与标题栏用 $(panel)，页签未选中 $(window)、选中
// $(panel) + 主色下划线 + 加粗文字，并用 min-height 固定高度防止内容压行。
const QString kDockQss = QStringLiteral(R"(
ads--CDockContainerWidget { background: $(panel); }
ads--CDockContainerWidget ads--CDockSplitter::handle { background: $(border); }
ads--CDockAreaWidget { background: $(panel); }
ads--CDockAreaTitleBar { background: $(panel); border-bottom: 1px solid $(border); }
ads--CDockWidget { background: $(panel); border: none; }
ads--CDockWidgetTab {
    background: $(window);
    border: none;
    border-right: 1px solid $(border);
    padding: 0 6px;
    min-height: 28px;
}
ads--CDockWidgetTab[activeTab="true"] {
    background: $(panel);
    border-bottom: 2px solid $(accent);
}
ads--CDockWidgetTab QLabel { color: $(textMuted); background: transparent; }
ads--CDockWidgetTab[activeTab="true"] QLabel {
    color: $(text);
    font-weight: 600;
    background: transparent;
}
)");

QString themeName(ThemeManager::Theme theme)
{
    return theme == ThemeManager::Theme::Dark ? QStringLiteral("dark")
                                              : QStringLiteral("light");
}

uischema::ThemeTokens::Theme toTokensTheme(ThemeManager::Theme theme)
{
    return theme == ThemeManager::Theme::Dark ? uischema::ThemeTokens::Theme::Dark
                                              : uischema::ThemeTokens::Theme::Light;
}

} // namespace

ThemeManager::ThemeManager(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_current = load();
}

QString ThemeManager::currentName() const
{
    return themeName(m_current);
}

ThemeManager::Theme ThemeManager::load() const
{
    const QString name = m_settings.value(QString::fromLatin1(kThemeKey), QStringLiteral("light"))
                             .toString();
    return name == QStringLiteral("dark") ? Theme::Dark : Theme::Light;
}

void ThemeManager::persist() const
{
    m_settings.setValue(QString::fromLatin1(kThemeKey), themeName(m_current));
}

void ThemeManager::applyCurrent()
{
    g_activeTheme = m_current; // 进程级当前主题：delegate 取色用
    apply();
    emit themeChanged(m_current);
}

void ThemeManager::setTheme(Theme theme)
{
    if (theme == m_current)
        return;
    m_current = theme;
    persist();
    applyCurrent();
}

void ThemeManager::toggle()
{
    setTheme(m_current == Theme::Dark ? Theme::Light : Theme::Dark);
}

ThemeManager::Theme ThemeManager::activeTheme()
{
    return g_activeTheme;
}

QColor ThemeManager::themeColor(const QString &token)
{
    return color(g_activeTheme, token);
}

void ThemeManager::setExtensionTokens(
    const QList<nextpilot::plotscope::registry::ThemeTokenSpec> &tokens)
{
    m_extTokens.clear();
    for (const auto &spec : tokens)
    {
        QColor c(spec.fallback);
        if (!c.isValid())
            continue; // 兜底色非法就不登记：宁可不出现，也不登记一个洋红
        m_extTokens.insert(spec.id, c);
    }
}

QColor ThemeManager::color(const QString &token) const
{
    const QColor base = color(m_current, token);
    // 内置色优先：table() 命中即返回；没命中才看包声明的兜底色。
    if (base != QColor(QStringLiteral("#FF00FF")))
        return base;
    auto it = m_extTokens.constFind(token);
    if (it != m_extTokens.constEnd())
        return it.value();
    return base;
}

QColor ThemeManager::color(Theme theme, const QString &token)
{
    return uischema::ThemeTokens::color(toTokensTheme(theme), token);
}

QColor ThemeManager::seriesColor(int index, Theme theme)
{
    return uischema::ThemeTokens::seriesColor(index, toTokensTheme(theme));
}

QColor ThemeManager::adaptColor(const QColor &c, Theme theme)
{
    return uischema::ThemeTokens::adaptColor(c, toTokensTheme(theme));
}

QString ThemeManager::mapStyleSheet(const QString &fragment) const
{
    QString out = fragment;
    // 遍历整套色表，把 $(token) 换成当前主题实色。
    const auto &t = uischema::ThemeTokens::table(toTokensTheme(m_current));
    for (auto it = t.constBegin(); it != t.constEnd(); ++it)
        out.replace(QStringLiteral("$(") + it.key() + QStringLiteral(")"), it.value().name());
    return out;
}

QString ThemeManager::styleSheet() const
{
    return mapStyleSheet(kGlobalQss);
}

QString ThemeManager::ribbonStyleSheet() const
{
    return mapStyleSheet(kRibbonQss);
}

QString ThemeManager::dockStyleSheet() const
{
    return mapStyleSheet(kDockQss);
}

QPalette ThemeManager::palette() const
{
    // Fusion 风格完全服从 palette，故画布/SARibbon 等吃 palette 的控件靠它换肤。
    QPalette p;
    p.setColor(QPalette::Window,        color(kWindow));
    p.setColor(QPalette::WindowText,    color(kText));
    p.setColor(QPalette::Base,          color(kBase));
    p.setColor(QPalette::AlternateBase, color(kPanel));
    p.setColor(QPalette::Text,          color(kText));
    p.setColor(QPalette::Button,        color(kPanel));
    p.setColor(QPalette::ButtonText,    color(kText));
    p.setColor(QPalette::Light,         color(kTextDim));
    p.setColor(QPalette::Midlight,      color(kPanel));
    p.setColor(QPalette::Mid,           color(kBorder));
    p.setColor(QPalette::Dark,          color(kWindow));
    p.setColor(QPalette::Highlight,     color(kAccent));
    p.setColor(QPalette::HighlightedText, color(kAccentText));
    p.setColor(QPalette::ToolTipBase,   color(kPanel));
    p.setColor(QPalette::ToolTipText,   color(kText));
    p.setColor(QPalette::PlaceholderText, color(kTextMuted));
    p.setColor(QPalette::Disabled, QPalette::Text,       color(kTextMuted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, color(kTextMuted));
    p.setColor(QPalette::Disabled, QPalette::WindowText, color(kTextMuted));
    return p;
}

void ThemeManager::apply()
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;
    // Fusion：完全服从 palette 的风格，避免原生风格残留浅色；配色全部走 token。
    if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app->setStyle(fusion);
    app->setPalette(palette());
    app->setStyleSheet(styleSheet());
}

bool ThemeManager::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("theme.set") ||
           intent.op == QStringLiteral("theme.toggle") ||
           intent.op == QStringLiteral("theme.get");
}

Result<IntentResult> ThemeManager::execute(const Intent &intent)
{
    if (intent.op == QStringLiteral("theme.toggle"))
    {
        toggle();
        IntentResult r;
        r.payload = currentName();
        return r;
    }
    if (intent.op == QStringLiteral("theme.set"))
    {
        const QString name = intent.params.value(QStringLiteral("theme"),
                                                 QStringLiteral("light")).toString();
        setTheme(name == QStringLiteral("dark") ? Theme::Dark : Theme::Light);
        IntentResult r;
        r.payload = currentName();
        return r;
    }
    if (intent.op == QStringLiteral("theme.get"))
    {
        IntentResult r;
        r.payload = currentName();
        return r;
    }
    return fail(err::Unsupported,
                QStringLiteral("未知主题意图: %1").arg(intent.op));
}

} // namespace nextpilot::plotscope::gui
