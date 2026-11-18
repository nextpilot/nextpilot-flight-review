#include "ContributionKind.h"

namespace ng::core
{

QString kindName(const QString &kind)
{
    if (kind == ContributionKind::Format)  return QStringLiteral("数据格式");
    if (kind == ContributionKind::Ui)      return QStringLiteral("界面贡献");
    if (kind == ContributionKind::Script)  return QStringLiteral("脚本");
    if (kind == ContributionKind::Data)    return QStringLiteral("数据");
    if (kind == ContributionKind::Example) return QStringLiteral("例子");
    if (kind == ContributionKind::Doc)     return QStringLiteral("文档");
    return kind; // 自定义 kind：原样展示
}

QStringList knownKinds()
{
    return {
        ContributionKind::Format,
        ContributionKind::Ui,
        ContributionKind::Script,
        ContributionKind::Data,
        ContributionKind::Example,
        ContributionKind::Doc,
    };
}

} // namespace ng::core
