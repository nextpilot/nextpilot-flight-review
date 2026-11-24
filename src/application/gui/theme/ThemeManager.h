#pragma once

#include "SettingsStore.h"
#include "Intent.h"
#include "Result.h"
#include "Contribution.h"

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

namespace nextpilot::plotscope::gui
{

// 主题管理：语义 token -> 双主题色值 -> QSS，一条链收口。
//
// 设计：
//   · **token 化**：颜色以语义 token 表达（text / textDim / accent ...），
//     十六进制色值只在 ThemeManager.cpp 的**唯一色表**里出现一次；
//     视图代码一律 ThemeManager::color(token) / styleSheet()，符合根 AGENTS §12.1。
//   · **无状态渲染 + 有状态管理合一**：色表是纯函数（给 theme+token 出 QColor），
//     当前主题状态与持久化是实例职责；拆成两个类只会为传 theme 参数互相纠缠。
//   · **不认识第三方**：ThemeManager 只产出 token/色/QSS 并发 themeChanged；
//     SARibbon / ADS 的换肤由绑定器订阅 themeChanged 自己套用（ARCHITECTURE §7：
//     只有绑定器知道 SARibbon/ADS）。于是 core/host 与 GUI 第三方都不污染。
//   · **走意图**：theme.set / theme.toggle / theme.get 由本类认领。按钮只声明
//     intentOp，点击 -> dispatcher -> 意图 -> 本类，与 CLI/脚本同一条路径。
//
// 为什么是 IIntentHandler 而不是宿主成员：主题是纯 GUI 展示态，headless 无界面，
// 不该在 headless 宿主里凭空存在一个"切主题"处理器。GUI 装配时把本实例注册为
// 系统级 handler（@system/theme），于是 CLI 收到 theme.* 会诚实报"无处理器"。
class ThemeManager final : public QObject, public nextpilot::plotscope::intent::IIntentHandler
{
    Q_OBJECT
public:
    enum class Theme
    {
        Light,
        Dark
    };
    Q_ENUM(Theme)

    explicit ThemeManager(nextpilot::plotscope::context::SettingsStore &settings, QObject *parent = nullptr);

    // --- 当前主题状态 ---
    Theme current() const { return m_current; }
    QString currentName() const;

    // 扩展包声明的语义色（ThemeTokenSpec）：表里没有的 token 才用它，
    // 于是内置色永远优先、包色不会覆盖产品外观。fallback 非法时按洋红处理，
    // 与"未知 token"一致——不能悄悄退化成黑/白。
    void setExtensionTokens(const QList<nextpilot::plotscope::registry::ThemeTokenSpec> &tokens);
    void setTheme(Theme theme);
    void toggle();

    // 把当前主题完整应用一遍（全局 QSS + 发 themeChanged 让 ribbon 跟上）。
    // 启动时调用：把 settings 里读到的初始值真正落到界面上。
    void applyCurrent();

    // --- token 取色（§12.1：UI 颜色的唯一来源）---
    QColor color(const QString &token) const;         // 当前主题
    static QColor color(Theme theme, const QString &token); // 指定主题（纯函数，可测试）
    static QColor seriesColor(int index, Theme theme);      // 数据语义色集中定义
    static QColor adaptColor(const QColor &c, Theme theme); // 按主题微调色，保证可读

    // --- 进程级当前主题（供 delegate 绘制时取色）---
    //
    // 为什么需要静态入口：视图的绘制委托（如数据源列表的状态圆点）在
    // paint 时才需要取色，而视图由工厂创建、拿不到 ThemeManager 实例。
    // 静态入口让"绘制时动态取色"成立（§12.5：树/表用 delegate 动态取色），
    // 且颜色自动跟随主题切换，无需额外信号。
    static Theme activeTheme();
    static QColor themeColor(const QString &token);

    // --- QSS 生成 ---
    // 全局样式（QApplication::setStyleSheet）。QSS 模板中的颜色全部来自 token。
    QString styleSheet() const;
    // 与 QSS 配套的调色板：SARibbon 内部、画布等吃 palette 的控件靠它换肤。
    // 所有颜色同样来自 token，不含硬编码。
    QPalette palette() const;
    // SARibbon 专属覆盖 QSS：设给 ribbonBar 本身，解决它自绘/内建主题压过
    // 全局 QSS 的问题。颜色同样全部来自 token。
    QString ribbonStyleSheet() const;
    // ADS 停靠区页签条样式：ADS 自带的是 linux 浅色 palette 规则，与主题不符，
    // 且各停靠组外观会分裂。这里用 token 重写一套，四个停靠组（画布/左/右/底）
    // 统一为「未选中灰底、选中白底 + 主色下划线、选中文字加粗」。
    QString dockStyleSheet() const;
    // 运行时内联样式：把 QSS 片段里的 $(token) 占位符替换成当前主题的实色。
    // §12.1：内联样式必须当场 map，不能依赖启动扫描（后加的控件才不会残留旧主题色）。
    QString mapStyleSheet(const QString &fragment) const;

    // --- IIntentHandler ---
    bool canHandle(const nextpilot::plotscope::intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> execute(
        const nextpilot::plotscope::intent::Intent &intent) override;

    // app/theme 在 settings 中的键（app 作用域，跨工作区）。
    static constexpr const char *kThemeKey = "app/theme";

Q_SIGNALS:
    void themeChanged(nextpilot::plotscope::gui::ThemeManager::Theme theme);

private:
    void apply();                       // 应用全局 QSS 到 QApplication
    void persist() const;               // 写 settings
    Theme load() const;                 // 从 settings 读，缺省 Light

    nextpilot::plotscope::context::SettingsStore &m_settings;
    Theme m_current = Theme::Light;
    QHash<QString, QColor> m_extTokens;
};

} // namespace nextpilot::plotscope::gui
