#include "GnssPack.h"

#include "BuiltinContributions.h"
#include "BuiltinPackRegistry.h"

using namespace ng::core;

namespace ng::ext
{

QString GnssPack::packId()
{
    return QStringLiteral("pack.gnss");
}

std::unique_ptr<IToolPack> GnssPack::build()
{
    // 预留领域：卫星导航协议（NMEA / UBX / OEM7）格式后续平移时落入本包。
    auto pack = std::make_unique<ToolPack>(packId(), QStringLiteral("卫导工具箱"), FormatDomain::Gnss);
    pack->add<GenericScriptContribution>(
        QStringLiteral("gnss.script.helpers"), QStringLiteral("卫导脚本库"), FormatDomain::Gnss,
        QStringLiteral("lua"),
        QStringLiteral("ps.gnss = { quality = function() ps.log('gnss quality helpers') end }\n"),
        QStringList{QStringLiteral("ps.gnss")});
    pack->add<GenericDocContribution>(
        QStringLiteral("gnss.docs"), QStringLiteral("卫导知识文档"), FormatDomain::Gnss,
        QList<DocEntry>{
            {QStringLiteral("gnss.doc.nmea"), QStringLiteral("NMEA / UBX / OEM7（待接入）"),
             QStringLiteral("docs/gnss/nmea.md"), QStringLiteral("text/markdown")}});
    return pack;
}

QString GnssPack::id() const { return m_pack.id(); }
QString GnssPack::displayName() const { return m_pack.displayName(); }
FormatDomain GnssPack::domain() const { return m_pack.domain(); }
const std::vector<std::unique_ptr<IToolContribution>> &GnssPack::contributions() const
{
    return m_pack.contributions();
}
std::vector<std::unique_ptr<IToolContribution>> &GnssPack::contributions()
{
    return m_pack.contributions();
}

} // namespace ng::ext

NG_REGISTER_PACK(::ng::ext::GnssPack)
