#include "ContributionKind.h"

namespace nextpilot::plotscope::core
{

QString kindName(const QString &kind)
{
    if (kind == ContributionKind::Format)  return QStringLiteral("数据格式");
    if (kind == ContributionKind::Ui)      return QStringLiteral("界面贡献");
    if (kind == ContributionKind::Panel)   return QStringLiteral("领域面板");
    if (kind == ContributionKind::Script)  return QStringLiteral("脚本");
    if (kind == ContributionKind::Data)    return QStringLiteral("数据");
    if (kind == ContributionKind::Example) return QStringLiteral("例子");
    if (kind == ContributionKind::Doc)     return QStringLiteral("文档");
    if (kind == ContributionKind::Message) return QStringLiteral("协议解析器");
    return kind; // 自定义 kind：原样展示
}

QStringList knownKinds()
{
    return {
        ContributionKind::Format,
        ContributionKind::Ui,
        ContributionKind::Panel,
        ContributionKind::Script,
        ContributionKind::Data,
        ContributionKind::Example,
        ContributionKind::Doc,
        ContributionKind::Message,
    };
}

} // namespace nextpilot::plotscope::core
