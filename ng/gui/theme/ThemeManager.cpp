#include "ThemeManager.h"
#include "ErrorCodes.h"

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
// 语义 token 清单。视图代码只认这些名字，绝不认色值。
// 命名沿用根 AGENTS §12.1 的契约：text / textDim / textMuted。
const QString kWindow      = QStringLiteral("window");       // 顶层容器背景
const QString kPanel       = QStringLiteral("panel");        // 面板/停靠区背景
const QString kBase        = QStringLiteral("base");         // 输入/表格底色
const QString kText        = QStringLiteral("text");         // 正文
const QString kTextDim     = QStringLiteral("textDim");      // 次要信息
const QString kTextMuted   = QStringLiteral("textMuted");    // 禁用/占位
const QString kBorder      = QStringLiteral("border");       // 分隔线/边框
const QString kAccent      = QStringLiteral("accent");       // 选中/高亮
const QString kAccentText  = QStringLiteral("accentText");   // 高亮背景上的文字
const QString kSelection   = QStringLiteral("selection");    // 选中行背景
const QString kSelectionTx = QStringLiteral("selectionText");// 选中行文字
const QString kGrid        = QStringLiteral("grid");         // 网格线
// 状态语义色（源状态点等）：深浅两套各自可读。
const QString kStateOk     = QStringLiteral("stateOk");      // 运行中
const QString kStateIdle   = QStringLiteral("stateIdle");    // 已停止
const QString kStateError  = QStringLiteral("stateError");   // 错误
const QString kStateWarn   = QStringLiteral("stateWarn");    // 警告/暂停

// 色表：全工程**唯一**允许出现色值的地方（ThemeManager 自身即色源）。
// 两套主题各一份，token 名一一对应。新增 token 必须同时填两套，
// 否则 color() 会给出醒目的洋红告警色——测试里也断言了这点。
const QHash<QString, QColor> &table(ThemeManager::Theme theme)
{
    static const QHash<QString, QColor> light = {
        {kWindow,      QColor(QStringLiteral("#F5F6F8"))},
        {kPanel,       QColor(QStringLiteral("#FFFFFF"))},
        {kBase,        QColor(QStringLiteral("#FFFFFF"))},
        {kText,        QColor(QStringLiteral("#1F2329"))},
        {kTextDim,     QColor(QStringLiteral("#646A73"))},
        {kTextMuted,   QColor(QStringLiteral("#8F959E"))},
        {kBorder,      QColor(QStringLiteral("#DEE0E3"))},
        {kAccent,      QColor(QStringLiteral("#2E7BEF"))},
        {kAccentText,  QColor(QStringLiteral("#FFFFFF"))},
        {kSelection,   QColor(QStringLiteral("#D6E6FF"))},
        {kSelectionTx, QColor(QStringLiteral("#1F2329"))},
        {kGrid,        QColor(QStringLiteral("#EDEEF0"))},
        {kStateOk,     QColor(QStringLiteral("#2EAF7D"))},
        {kStateIdle,   QColor(QStringLiteral("#6E7681"))},
        {kStateError,  QColor(QStringLiteral("#E5484D"))},
        {kStateWarn,   QColor(QStringLiteral("#D4A72C"))},
    };
    static const QHash<QString, QColor> dark = {
        {kWindow,      QColor(QStringLiteral("#1E1F22"))},
        {kPanel,       QColor(QStringLiteral("#2B2D31"))},
        {kBase,        QColor(QStringLiteral("#25262A"))},
        {kText,        QColor(QStringLiteral("#E6E8EB"))},
        {kTextDim,     QColor(QStringLiteral("#A6A9B0"))},
        {kTextMuted,   QColor(QStringLiteral("#71757E"))},
        {kBorder,      QColor(QStringLiteral("#3E4148"))},
        {kAccent,      QColor(QStringLiteral("#4C9AFF"))},
        {kAccentText,  QColor(QStringLiteral("#101114"))},
        {kSelection,   QColor(QStringLiteral("#2F4E7E"))},
        {kSelectionTx, QColor(QStringLiteral("#E6E8EB"))},
        {kGrid,        QColor(QStringLiteral("#34363C"))},
        {kStateOk,     QColor(QStringLiteral("#3FCF8E"))},
        {kStateIdle,   QColor(QStringLiteral("#71757E"))},
        {kStateError,  QColor(QStringLiteral("#FF6369"))},
        {kStateWarn,   QColor(QStringLiteral("#F5C451"))},
    };
    return theme == ThemeManager::Theme::Dark ? dark : light;
}

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
QMenuBar, QMenu, QToolTip {
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

QString themeName(ThemeManager::Theme theme)
{
    return theme == ThemeManager::Theme::Dark ? QStringLiteral("dark")
                                              : QStringLiteral("light");
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

QColor ThemeManager::color(const QString &token) const
{
    return color(m_current, token);
}

QColor ThemeManager::color(Theme theme, const QString &token)
{
    const auto &t = table(theme);
    auto it = t.constFind(token);
    if (it != t.constEnd())
        return it.value();
    // 未知 token 给醒目洋红：缺失 token 一眼可见，而不是静默退化成黑/白。
    return QColor(QStringLiteral("#FF00FF"));
}

QColor ThemeManager::seriesColor(int index, Theme theme)
{
    // 分类色板集中在此（§12.1：seriesColor 不得散落业务代码）。
    static const QList<QColor> palette = {
        QColor(QStringLiteral("#2E7BEF")), QColor(QStringLiteral("#E8633A")),
        QColor(QStringLiteral("#2EAF7D")), QColor(QStringLiteral("#A55BEE")),
        QColor(QStringLiteral("#D4A72C")), QColor(QStringLiteral("#26A6C1")),
        QColor(QStringLiteral("#E45C9B")), QColor(QStringLiteral("#7A8450")),
        QColor(QStringLiteral("#5A6FE8")), QColor(QStringLiteral("#C9772B")),
        QColor(QStringLiteral("#379B6B")), QColor(QStringLiteral("#9B5CC4")),
    };
    if (palette.isEmpty())
        return QColor();
    const QColor base = palette.at(((index % palette.size()) + palette.size()) % palette.size());
    return adaptColor(base, theme);
}

QColor ThemeManager::adaptColor(const QColor &c, Theme theme)
{
    // 让同一个语义色在两套背景上都可读：暗色主题的系列色向白提亮，
    // 亮色主题向黑压暗。用温和混合比例，保留原色相。
    const QColor target = theme == Theme::Dark ? QColor(QStringLiteral("#FFFFFF"))
                                               : QColor(QStringLiteral("#101010"));
    const int amount = theme == Theme::Dark ? 18 : 12;
    return QColor::fromRgb(int(c.red() + (target.red() - c.red()) * amount / 100),
                           int(c.green() + (target.green() - c.green()) * amount / 100),
                           int(c.blue() + (target.blue() - c.blue()) * amount / 100));
}

QString ThemeManager::mapStyleSheet(const QString &fragment) const
{
    QString out = fragment;
    // 遍历整套色表，把 $(token) 换成当前主题实色。
    const auto &t = table(m_current);
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
    // Fusion：完全服从 palette 的风格，避免原生风格残留浅色（老代码同款做法，
    // 但配色全部走 token，不抄它的硬编码十六进制）。
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
