#include "FormatToolbox.h"

#include "CsvFormatDriver.h"
#include "UlogFormatDriver.h"

#include <memory>

namespace ng::ext
{

void registerBuiltinFormats(ng::core::ExtensionRegistry &registry)
{
    // 内置格式驱动集中自注册。新增一种格式：加一行即可，app 与器官都不用改。
    registry.registerDriver(std::make_unique<CsvFormatDriver>());
    registry.registerDriver(std::make_unique<UlogFormatDriver>());
}

} // namespace ng::ext
