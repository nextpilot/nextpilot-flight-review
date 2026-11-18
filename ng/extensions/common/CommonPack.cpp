#include "CommonPack.h"

#include "BuiltinContributions.h"
#include "BuiltinPackRegistry.h"
#include "CsvFormatDriver.h"
#include "CustomProtocolFormatDriver.h"

using namespace ng::core;

namespace ng::ext
{

QString CommonPack::packId()
{
    return QStringLiteral("pack.common");
}

std::unique_ptr<IToolPack> CommonPack::build()
{
    auto pack = std::make_unique<ToolPack>(packId(), QStringLiteral("通用工具箱"), FormatDomain::General);
    pack->add<CsvFormatDriver>(); // 格式：文件 -> Dataset
    pack->add<CustomProtocolFormatDriver>(); // 格式：.psproto 模板 + 二进制帧 -> Dataset
    pack->add<GenericDocContribution>(
        QStringLiteral("common.doc.formats"), QStringLiteral("通用格式说明"), FormatDomain::General,
        QList<DocEntry>{
            {QStringLiteral("common.doc.csv"), QStringLiteral("CSV 载入指南"),
             QStringLiteral("docs/common/csv.md"), QStringLiteral("text/markdown")}});
    return pack;
}

QString CommonPack::id() const { return m_pack.id(); }
QString CommonPack::displayName() const { return m_pack.displayName(); }
FormatDomain CommonPack::domain() const { return m_pack.domain(); }
const std::vector<std::unique_ptr<IToolContribution>> &CommonPack::contributions() const
{
    return m_pack.contributions();
}
std::vector<std::unique_ptr<IToolContribution>> &CommonPack::contributions()
{
    return m_pack.contributions();
}

} // namespace ng::ext

NG_REGISTER_PACK(::ng::ext::CommonPack)
