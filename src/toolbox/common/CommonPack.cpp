#include "CommonPack.h"

#include "Contribution.h"
#include "CsvFormatParser.h"
#include "CustomProtocolFormatParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

namespace nextpilot::plotscope::ext
{

QString CommonPack::packId()
{
    return QStringLiteral("pack.common");
}

std::unique_ptr<Pack> CommonPack::build()
{
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("通用工具箱");
    pack->domain = Domain::General;

    pack->parsers.push_back(std::make_unique<CsvFormatParser>());            // 格式：文件 -> Dataset
    pack->parsers.push_back(std::make_unique<CustomProtocolFormatParser>()); // 格式：.psproto 模板 + 二进制帧

    pack->docs.append({
        {QStringLiteral("common.doc.csv"), QStringLiteral("CSV 载入指南"),
         QStringLiteral("docs/common/csv.md"), QStringLiteral("text/markdown")}});

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::CommonPack)
