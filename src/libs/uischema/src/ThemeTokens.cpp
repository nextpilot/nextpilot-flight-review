#include "ThemeTokens.h"

namespace nextpilot::plotscope::uischema
{

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

// 色表：全工程**唯一**允许出现色值的地方（本表即色源）。
// 两套主题各一份，token 名一一对应。新增 token 必须同时填两套，
// 否则 color() 会给出醒目的洋红告警色——测试里也断言了这点。
const QHash<QString, QColor> &table(ThemeTokens::Theme theme)
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
    return theme == ThemeTokens::Theme::Dark ? dark : light;
}
} // namespace

const QHash<QString, QColor> &ThemeTokens::table(Theme theme)
{
    return ::table(theme);
}

QColor ThemeTokens::color(Theme theme, const QString &token)
{
    const auto &t = ::table(theme);
    auto it = t.constFind(token);
    if (it != t.constEnd())
        return it.value();
    // 未知 token 给醒目洋红：缺失 token 一眼可见，而不是静默退化成黑/白。
    return QColor(QStringLiteral("#FF00FF"));
}

QColor ThemeTokens::seriesColor(int index, Theme theme)
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

QColor ThemeTokens::adaptColor(const QColor &c, Theme theme)
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

} // namespace nextpilot::plotscope::uischema
