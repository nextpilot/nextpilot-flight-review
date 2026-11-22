#include "ApbinPack.h"

#include "Contribution.h"
#include "ApbinFormatParser.h"

using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::ext
{

QString ApbinPack::packId()
{
    return QStringLiteral("pack.apbin");
}

std::unique_ptr<Pack> ApbinPack::build()
{
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("ArduPilot DataFlash");
    pack->domain = Domain::Autopilot;

    // 格式：DataFlash（.bin/.log）→ Dataset（数值批经 SPI 解析器 apbin 执行）
    pack->parsers.push_back(std::make_unique<ApbinFormatParser>());

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::ApbinPack)
