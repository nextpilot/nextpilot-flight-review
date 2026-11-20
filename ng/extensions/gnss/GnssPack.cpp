#include "GnssPack.h"

#include "Contribution.h"
#include "NmeaMessageParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

namespace nextpilot::plotscope::ext
{

QString GnssPack::packId()
{
    return QStringLiteral("pack.gnss");
}

std::unique_ptr<Pack> GnssPack::build()
{
    // 预留领域：卫星导航协议（NMEA / UBX / OEM7）格式后续平移时落入本包。
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("卫导工具箱");
    pack->domain = Domain::Gnss;

    // 协议解析器：NMEA 0183 —— 自述 + configure + parse 的样板实现。
    pack->parsers.push_back(std::make_unique<NmeaMessageParser>());

    pack->scripts.append({
        {QStringLiteral("lua"),
         QStringLiteral("ps.gnss = { quality = function() ps.log('gnss quality helpers') end }\n"),
         QStringList{QStringLiteral("ps.gnss")},
         QStringLiteral("gnss.script.helpers"), QStringLiteral("卫导脚本库")}});

    pack->docs.append({
        {QStringLiteral("gnss.doc.nmea"), QStringLiteral("NMEA / UBX / OEM7（待接入）"),
         QStringLiteral("docs/gnss/nmea.md"), QStringLiteral("text/markdown")}});

    return pack;
}

} // namespace nextpilot::plotscope::ext

NG_REGISTER_PACK(::nextpilot::plotscope::ext::GnssPack)
