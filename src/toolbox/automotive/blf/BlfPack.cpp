#include "BlfPack.h"

#include "Contribution.h"
#include "BlfFormatParser.h"

using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::ext
{

QString BlfPack::packId()
{
    return QStringLiteral("pack.blf");
}

std::unique_ptr<Pack> BlfPack::build()
{
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("Vector BLF");
    pack->domain = Domain::Automotive;

    // 格式：Vector BLF（.blf）→ Dataset（数值批经 SPI 解析器 blf 执行）
    pack->parsers.push_back(std::make_unique<BlfFormatParser>());

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::BlfPack)
