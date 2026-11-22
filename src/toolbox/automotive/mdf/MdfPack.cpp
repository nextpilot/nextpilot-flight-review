#include "MdfPack.h"

#include "Contribution.h"
#include "MdfFormatParser.h"

using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::ext
{

QString MdfPack::packId()
{
    return QStringLiteral("pack.mdf");
}

std::unique_ptr<Pack> MdfPack::build()
{
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("MDF 测量文件");
    pack->domain = Domain::Automotive;

    // 格式：MDF 4.x → Dataset（数值批经 SPI 解析器 mdf4 执行）
    pack->parsers.push_back(std::make_unique<MdfFormatParser>());

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::MdfPack)
