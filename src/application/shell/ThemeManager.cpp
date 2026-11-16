#include "ThemeManager.h"
#include "SettingsManager.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include <QVector>
#include <QWidget>

namespace
{
bool g_dark = true;

struct Token
{
    const char *key;
    const char *dark;
    const char *light;
};

const Token kTokens[] = {
    {"bg", "#0d1117", "#f4f6f8"},
    {"dialog", "#0f1419", "#ffffff"},
    {"panel", "#161b20", "#ffffff"},
    {"panelAlt", "#1a2127", "#eef1f4"},
    {"input", "#0b0f13", "#ffffff"},
    {"border", "#232b33", "#d5dce3"},
    {"line", "#2d3841", "#b9c4cf"},
    {"text", "#c9d1d9", "#1f2933"},
    {"textDim", "#8494a0", "#5b6875"},
    {"textMuted", "#5d6c77", "#7a8794"},
    {"accent", "#f2b134", "#b45309"},
    {"hover", "#1b232a", "#e8edf2"},
    {"selection", "#1a2127", "#dbeafe"},
    {"canvasBg", "#10161c", "#ffffff"},
    {"grid", "#1c2430", "#dfe5ea"},
    {"status", "#0b1015", "#eef1f4"},
    {"danger", "#f85149", "#c62828"},
    {"success", "#7ee787", "#1b7f3a"},
    {"info", "#39c5cf", "#0e7490"},
};

const char *kDarkStyle = R"(
QWidget { background:#0d1117; color:#c9d1d9; font-size:12px; }
QDialog { background:#0f1419; }
QMainWindow::separator { background:#0f1419; width:1px; height:1px; }
QMainWindow::separator:hover { background:#2d3841; }

/* ---- 停靠区 ---- */
QDockWidget { border:1px solid #1b232a; titlebar-close-icon:none; }
QDockWidget::title { background:#161b20; color:#8494a0; padding:5px 8px; border-bottom:1px solid #232b33; }
QLabel#dockWidgetTabLabel { background:transparent; color:#c9d1d9; }

/* ---- 工具条 / 按钮 ---- */
QToolBar { background:#161b20; border:0; spacing:2px; padding:3px 6px; }
QToolButton { background:transparent; color:#8494a0; padding:4px 8px; border:1px solid transparent; border-radius:2px; }
QToolButton:hover { background:#1b232a; color:#c8d2da; border-color:#222b33; }
QToolButton:pressed { background:#212a31; }
QPushButton { background:#1b232a; border:1px solid #2d3841; color:#c9d1d9; padding:4px 16px; border-radius:2px; font-size:11.5px; }
QPushButton:hover { border-color:#f2b134; color:#f2b134; }
QPushButton:pressed { background:#212a31; }
QPushButton:disabled { color:#4a545d; border-color:#222b33; }

/* ---- 输入类 ---- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background:#0b0f13; border:1px solid #2d3841; color:#c9d1d9;
    padding:2px 6px; min-height:20px; selection-background-color:#f2b134; selection-color:#0d1117;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color:#f2b134; }
QComboBox::drop-down { border:0; width:16px; }
QComboBox::down-arrow { image:url(:/icons/arrow_more.svg); width:10px; height:10px; }
QComboBox QAbstractItemView { background:#0f1419; color:#c9d1d9; border:1px solid #2d3841; selection-background-color:#1b232a; }
QCheckBox { color:#c9d1d9; font-size:11.5px; spacing:6px; }
QCheckBox::indicator { width:12px; height:12px; border:1px solid #2d3841; background:#0b0f13; }
QCheckBox::indicator:checked { background:#f2b134; border-color:#f2b134; }
QGroupBox { color:#8494a0; border:1px solid #222b33; margin-top:9px; font-size:11px; }
QGroupBox::title { subcontrol-origin:margin; left:9px; padding:0 4px; }

/* ---- 页签（固定绘图区/对话框通用） ---- */
QTabWidget::pane { border:0; background:#0d1117; }
QTabBar { background:#0d1117; border:0; border-top:1px solid #232b33; }
QTabBar::tab {
    background:#161b20; color:#8494a0; font-size:12px;
    padding:4px 12px; margin-right:1px; border:0; border-bottom:2px solid transparent;
}
QTabBar::tab:selected { background:#1a2127; color:#c9d1d9; border-bottom:2px solid #f2b134; }
QTabBar::tab:hover:!selected { color:#c8d2da; background:#1a2127; }
QTabBar#pageTabs::tab:last { min-width:20px; padding:4px 8px; font-size:15px; color:#8494a0; }
QTabBar#pageTabs::tab:last:selected { color:#f2b134; }
QTabBar::close-button { image:url(:/icons/close_small.svg); subcontrol-position:right; margin:2px 4px; width:10px; height:10px; }
QTabBar::close-button:hover { background:#222b33; }

/* ---- 视图类 ---- */
QTreeWidget, QListWidget, QPlainTextEdit, QTextEdit {
    background:#0d1117; border:1px solid #21262d; alternate-background-color:#131a20;
    selection-background-color:#1b232a; selection-color:#c9d1d9; outline:0;
}
/* 参数/属性表：和正文同一底色，不靠条纹和边框抢视觉 */
QTableWidget {
    background:#0d1117; border:0; alternate-background-color:#0d1117;
    selection-background-color:#1b232a; selection-color:#c9d1d9; outline:0;
}
QTableWidget::item { padding:0 6px; border:0; }
QTableWidget::item:hover { background:#161d24; }
QTreeWidget::item { height:22px; border-left:2px solid transparent; }
QTreeWidget::item:hover { background:#1d242a; }
QTreeWidget::item:selected { background:#1a2127; color:#c9d1d9; border-left-color:#f2b134; }
QListWidget::item { padding:3px 6px; }
QListWidget::item:hover { background:#1d242a; }
QListWidget::item:selected { background:#1a2127; color:#c9d1d9; }
QTableWidget::item:selected { background:#1a2127; color:#c9d1d9; }
QHeaderView { background:transparent; border:0; }
QHeaderView::section { background:#0d1117; color:#6f7d88; border:0; border-bottom:1px solid #202830; padding:2px 6px; font-size:11px; }
QTableCornerButton::section { background:#161b20; border:0; }
QPlainTextEdit { font-size:11.5px; }

/* ---- 滚动条 / 分隔条 / 角落 ---- */
QScrollBar:vertical { background:#0d1117; width:10px; border:0; margin:0; }
QScrollBar::handle:vertical { background:#2d3841; min-height:24px; border-radius:5px; }
QScrollBar::handle:vertical:hover { background:#3d4a55; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
QScrollBar:horizontal { background:#0d1117; height:10px; border:0; margin:0; }
QScrollBar::handle:horizontal { background:#2d3841; min-width:24px; border-radius:5px; }
QScrollBar::handle:horizontal:hover { background:#3d4a55; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }
QAbstractScrollArea::corner { background:#0d1117; border:0; }
QSplitter::handle { background:#1b232a; }
QSplitter::handle:horizontal { width:1px; }
QSplitter::handle:vertical { height:1px; }
QSplitter::handle:hover { background:#2d3841; }

/* ---- 状态栏 / 提示 ---- */
QStatusBar { background:#161b20; color:#6e7681; border-top:1px solid #232b33; }
QStatusBar::item { border:0; }
SARibbonPanelLabel { background:transparent; color:#7d8b96; font-size:10px; }
QToolTip { background:#161b20; color:#c9d1d9; border:1px solid #232b33; padding:3px 6px; }
QMenu { background:#161b20; color:#c9d1d9; border:1px solid #2d3841; padding:4px; }
QMenu::item { padding:5px 22px 5px 18px; font-size:12px; }
QMenu::item:selected { background:#1f2a33; }
QMenu::item:disabled { color:#5d6c77; }
QMenu::separator { height:1px; background:#232b33; margin:4px 6px; }
QMenu::indicator { width:10px; height:10px; margin-left:4px; border-radius:5px; }
QMenu::indicator:checked { background:#f2b134; }
QMenu::indicator:unchecked { background:transparent; border:1px solid #2d3841; }
QMenu { background:#161b20; border:1px solid #2d3841; padding:4px; }
QMenu::item { padding:5px 24px 5px 20px; font-size:12px; color:#c9d1d9; background:#161b20; border-radius:2px; }
QMenu::item:selected { background:#1f2a33; color:#f2b134; }
QMenu::item:disabled { color:#5d6c77; }
QMenu::separator { height:1px; background:#232b33; margin:4px 8px; }
QMenu::indicator { width:12px; height:12px; left:6px; }
QMenu::indicator:checked { image:url(:/icons/check.svg); }
QMenu { background:#161b20; color:#c9d1d9; border:1px solid #2d3841; }
QMenu::item:selected { background:#1b232a; color:#f2b134; }
QMessageBox { background:#0f1419; }
QMessageBox QLabel { color:#c9d1d9; }
)";

const char *kSeriesDark[] = {"#4ea1ff", "#ffb454", "#7ee787", "#d2a8ff", "#ff7b72", "#39c5cf"};
const char *kSeriesLight[] = {"#1f6feb", "#b26a00", "#1b7f3a", "#7b3fbf", "#c62828", "#0e7490"};

struct Alias
{
    const char *dark;
    const char *light;
};

const Alias kAliases[] = {
    {"#0d1117", "#f4f6f8"}, {"#0f1419", "#ffffff"}, {"#141a1f", "#ffffff"},
    {"#161b20", "#ffffff"}, {"#222b33", "#d5dce3"},
    {"#1a2127", "#eef1f4"}, {"#232b33", "#d5dce3"}, {"#2d3841", "#b9c4cf"},
    {"#c9d1d9", "#1f2933"}, {"#8494a0", "#5b6875"}, {"#5d6c77", "#7a8794"},
    {"#f2b134", "#b45309"}, {"#4a545d", "#9aa7b2"}, {"#0b0f13", "#ffffff"},
    {"#1b232a", "#e8edf2"}, {"#1d242a", "#eef2f7"}, {"#21262d", "#d5dce3"},
    {"#202830", "#d5dce3"}, {"#131a20", "#eef2f7"}, {"#161d24", "#e8edf2"},
    {"#1f2a33", "#dbeafe"}, {"#6f7d88", "#64748b"}, {"#6e7681", "#64748b"},
    {"#7d8b96", "#64748b"}, {"#3d4a55", "#9aa7b2"}, {"#26313a", "#d5dce3"},
    {"#33414d", "#c7d2fe"}, {"#1b232b", "#e8edf2"}, {"#11171c", "#ffffff"},
    {"#0f151a", "#f4f6f8"}, {"#161d23", "#eef2f7"}, {"#2a343d", "#d5dce3"},
    {"#2a2416", "#fef3c7"}, {"#6b5320", "#d97706"}, {"#4a545e", "#9aa7b2"},
    {"#0b1015", "#eef1f4"}, {"#171f26", "#e2e8f0"}, {"#1c252d", "#dbeafe"},
    {"#0b1014", "#ffffff"}, {"#151d24", "#eef2f7"}, {"#d5dde5", "#1f2933"},
    {"#9aa7b2", "#475569"}, {"#f28b82", "#c62828"}, {"#7ee787", "#1b7f3a"},
    {"#39c5cf", "#0e7490"}, {"#4ea1ff", "#1f6feb"}, {"#d2a8ff", "#7b3fbf"},
    {"#ff7b72", "#c62828"}, {"#ffb454", "#b26a00"}, {"#c8d2da", "#1f2933"},
    {"#26303a", "#eef1f4"}, {"#3a4854", "#c7d2fe"}, {"#e6edf3", "#1f2933"},
    {"#6e7d88", "#64748b"}, {"#30363d", "#cbd5e1"}, {"#161b22", "#ffffff"},
    {"#21262d", "#d5dce3"}, {"#141a21", "#eef2f7"}, {"#8b949e", "#475569"},
    {"#e3b341", "#b45309"}, {"#221c0f", "#fef3c7"}, {"#3a2f16", "#d97706"},
    {"#f85149", "#c62828"}, {"#212a31", "#dbeafe"}, {"#1a2127", "#eef1f4"},
    {"#2a3b48", "#cbd5e1"}, {"#4a5a68", "#94a3b8"}, {"#2b3a4a", "#e2e8f0"},
    {"#3a2f22", "#fef3c7"},
};

QColor tokenColor(const char *dark, const char *light)
{
    return QColor(QString::fromLatin1(g_dark ? dark : light));
}

QString mapped(const QString &text)
{
    if (g_dark)
        return text;
    QString out = text;
    for (const Alias &a : kAliases)
        out.replace(QLatin1String(a.dark), QLatin1String(a.light));
    return out;
}

QPalette palette()
{
    QPalette p;
    const QColor bg = tokenColor("#0d1117", "#f4f6f8");
    const QColor bar = tokenColor("#161b20", "#ffffff");
    const QColor fg = tokenColor("#c9d1d9", "#1f2933");
    const QColor dim = tokenColor("#5d6c77", "#7a8794");
    const QColor amber = tokenColor("#f2b134", "#b45309");
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, bar);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Button, bar);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::Light, bar);
    p.setColor(QPalette::Midlight, bar);
    p.setColor(QPalette::Mid, tokenColor("#232b33", "#d5dce3"));
    p.setColor(QPalette::Dark, bg);
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, tokenColor("#1f6feb", "#0e639c"));
    p.setColor(QPalette::HighlightedText, tokenColor("#0d1117", "#ffffff"));
    p.setColor(QPalette::Link, amber);
    p.setColor(QPalette::ToolTipBase, bar);
    p.setColor(QPalette::ToolTipText, fg);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Disabled, QPalette::Text, dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dim);
    return p;
}
} // namespace

void ThemeManager::applyTo(QApplication &app)
{
    const QString saved = SettingsManager::instance().get(ps::settings::kTheme);
    g_dark = saved.compare(QLatin1String("light"), Qt::CaseInsensitive) != 0;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setStyleSheet(styleSheet());
    app.setPalette(palette());
}

void ThemeManager::setMode(Mode mode, QApplication *app)
{
    g_dark = mode == Mode::Dark;
    if (!app)
        app = qApp;
    if (!app)
        return;
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app->setStyleSheet(styleSheet());
    app->setPalette(palette());
    refreshWidgetStyles();
}

ThemeManager::Mode ThemeManager::mode()
{
    return g_dark ? Mode::Dark : Mode::Light;
}

bool ThemeManager::isDark()
{
    return g_dark;
}

QString ThemeManager::name()
{
    return g_dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QString ThemeManager::styleSheet()
{
    return mapped(QString::fromLatin1(kDarkStyle));
}

QColor ThemeManager::color(const QString &token)
{
    for (const Token &t : kTokens)
        if (token == QLatin1String(t.key))
            return tokenColor(t.dark, t.light);
    return tokenColor("#0d1117", "#f4f6f8");
}

QColor ThemeManager::seriesColor(int index)
{
    const int n = g_dark ? int(sizeof(kSeriesDark) / sizeof(kSeriesDark[0]))
                         : int(sizeof(kSeriesLight) / sizeof(kSeriesLight[0]));
    const int i = index % n;
    return QColor(QString::fromLatin1(g_dark ? kSeriesDark[i] : kSeriesLight[i]));
}

QColor ThemeManager::alphaColor(const QString &token, int alpha)
{
    QColor c = color(token);
    c.setAlpha(alpha);
    return c;
}

QColor ThemeManager::adaptColor(const QColor &color)
{
    if (g_dark)
        return color;
    QColor out(mapped(color.name()));
    out.setAlpha(color.alpha());
    return out;
}

QIcon ThemeManager::icon(const QString &path)
{
    if (g_dark)
        return QIcon(path);
    QPixmap pm = QIcon(path).pixmap(64, 64);
    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color(QStringLiteral("text")));
    p.end();
    // 线稿 SVG 多为半透明描边，浅色底上会被冲淡，抬一档不透明度
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
        {
            const int a = qAlpha(line[x]);
            if (a > 0)
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), qMin(255, a * 3));
        }
    }
    return QIcon(QPixmap::fromImage(img));
}

QString ThemeManager::mapStyleSheet(const QString &darkStyle)
{
    return mapped(darkStyle);
}

void ThemeManager::refreshWidgetStyles(QWidget *root)
{
    QVector<QWidget *> widgets;
    if (root)
    {
        widgets.append(root);
        const auto children = root->findChildren<QWidget *>();
        widgets.append(children.begin(), children.end());
    }
    else
    {
        widgets = QApplication::allWidgets().toVector();
    }
    for (QWidget *w : widgets)
    {
        if (!w)
            continue;
        if (w->property("psThemeManaged").toBool())
        {
            w->update();
            continue;
        }
        const QVariant original = w->property("psDarkStyle");
        QString darkStyle;
        if (original.isValid())
            darkStyle = original.toString();
        else if (!w->styleSheet().isEmpty())
        {
            darkStyle = w->styleSheet();
            w->setProperty("psDarkStyle", darkStyle);
        }
        else
            continue;
        w->setStyleSheet(g_dark ? darkStyle : mapped(darkStyle));
        w->update();
    }
}
