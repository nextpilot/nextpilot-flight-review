#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

// 全局主题 token + QSS。模块只取 token，不再各自维护深色十六进制表。
class ThemeManager
{
public:
    enum class Mode { Dark, Light };

    static void applyTo(QApplication &app);
    static void setMode(Mode mode, QApplication *app = nullptr);
    static Mode mode();
    static bool isDark();
    static QString name();
    static QString styleSheet();
    static QColor color(const QString &token);
    static QColor alphaColor(const QString &token, int alpha);
    static QColor seriesColor(int index);
    static QColor adaptColor(const QColor &color);
    // 单色图标按主题着色：深色主题原样，浅色主题反转成文字色
    static QIcon icon(const QString &path);
    static QString mapStyleSheet(const QString &darkStyle);
    static void refreshWidgetStyles(QWidget *root = nullptr);
};
