#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

// token（与原型 :root 对齐）
//   #0d1117 bg0   #161b20 panel   #1a2127 panel2   #232b33 line
//   #2d3841 line2 #c9d1d9 tx      #8494a0 tx2      #5d6c77 tx3
//   #f2b134 amber #7ee787 ok      #f85149 bad
const char *theme::kStyle = R"(
QWidget { background:#0d1117; color:#c9d1d9; font-size:12px; }
QDialog { background:#0f1419; }
QMainWindow::separator { background:#0f1419; width:1px; height:1px; }
QMainWindow::separator:hover { background:#2d3841; }

/* ---- 停靠区 ---- */
QDockWidget { border:1px solid #1b232a; titlebar-close-icon:none; }
QDockWidget::title { background:#161b20; color:#8494a0; padding:5px 8px; border-bottom:1px solid #232b33; }

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
QTabBar::close-button { image:url(:/icons/close_small.svg); subcontrol-position:right; margin:2px 3px; }
QTabBar::close-button:hover { background:#222b33; }

/* ---- 视图类 ---- */
QTreeWidget, QTableWidget, QListWidget, QPlainTextEdit, QTextEdit {
    background:#0d1117; border:1px solid #21262d; alternate-background-color:#131a20;
    selection-background-color:#1b232a; selection-color:#c9d1d9; outline:0;
}
QTreeWidget::item { height:22px; border-left:2px solid transparent; }
QTreeWidget::item:hover { background:#1d242a; }
QTreeWidget::item:selected { background:#1a2127; color:#c9d1d9; border-left-color:#f2b134; }
QListWidget::item { padding:3px 6px; }
QListWidget::item:hover { background:#1d242a; }
QListWidget::item:selected { background:#1a2127; color:#c9d1d9; }
QTableWidget::item:selected { background:#1a2127; color:#c9d1d9; }
QHeaderView { background:#161b20; border:0; }
QHeaderView::section { background:#161b20; color:#8494a0; border:0; border-bottom:1px solid #232b33; padding:5px 6px; font-size:11px; }
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
QMenu::item { padding:5px 24px 5px 20px; font-size:12px; color:#c9d1d9; border-radius:2px; }
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

void theme::applyTo(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setStyleSheet(QString::fromLatin1(kStyle));

    QPalette p;
    const QColor bg(QStringLiteral("#0d1117"));
    const QColor bar(QStringLiteral("#161b20"));
    const QColor fg(QStringLiteral("#c9d1d9"));
    const QColor dim(QStringLiteral("#5d6c77"));
    const QColor amber(QStringLiteral("#f2b134"));
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, bar);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Button, bar);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::Light, bar);
    p.setColor(QPalette::Midlight, bar);
    p.setColor(QPalette::Mid, QColor(QStringLiteral("#232b33")));
    p.setColor(QPalette::Dark, bg);
    p.setColor(QPalette::Shadow, QColor(QStringLiteral("#000000")));
    p.setColor(QPalette::Highlight, amber);
    p.setColor(QPalette::HighlightedText, bg);
    p.setColor(QPalette::Link, amber);
    p.setColor(QPalette::ToolTipBase, bar);
    p.setColor(QPalette::ToolTipText, fg);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Disabled, QPalette::Text, dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dim);
    app.setPalette(p);
}
