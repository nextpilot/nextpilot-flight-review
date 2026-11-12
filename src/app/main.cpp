// PlotScope 入口。Qt Widgets 单线程 UI（D1 / D1b / D1c），数据面全部走 ps-abi。
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

#include "ps_kernel.h"
#include "shell/MainWindow.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PlotScope"));
    QApplication::setOrganizationName(QStringLiteral("NextPilot"));

    // 全局深色调色板：Fusion 默认是浅色，dock 标题、tab 基线、滚动槽这些
    // 非 stylesheet 绘制的边线会露出白色，QSS 压不住，必须从 palette 断根
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    {
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
        QApplication::setPalette(p);
    }

    // 版本闸门：宿主只认自己声明的 ABI，不匹配直接拒启（D9）
    if (ps_abi_version() != PS_ABI_VERSION)
    {
        qFatal("ps-abi 版本不匹配：kernel=%u host=%u", ps_abi_version(), PS_ABI_VERSION);
    }

    MainWindow win;
    win.show();

    // 命令行带文件时直接加载：plotscope_app flight.csv
    if (argc > 1)
        win.loadFile(QString::fromLocal8Bit(argv[1]));

    return app.exec();
}
