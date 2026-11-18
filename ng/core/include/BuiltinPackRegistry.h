#pragma once

#include "IToolPack.h"

#include <QList>
#include <QString>

#include <functional>
#include <memory>

namespace ng::core
{

class ExtensionRegistry;

// 内置知识包的**编译期自注册**。
//
// 与 host::BuiltinModuleRegistry 同构：工具箱的每个领域包在自己的 TU 里静态登记，
// 宿主启动时遍历装载。于是"新增一个扩展（知识包）"= 新增一个目录 + 一行 CMake，
// 既有的 FormatToolbox / App / 宿主代码全都不动。
//
// 这里放 core 而非 extensions 层：注册表本体（ExtensionRegistry）在 core，
// 自注册表与它同处一层最自然。
class BuiltinPackRegistry
{
public:
    using PackFactory = std::function<std::unique_ptr<IToolPack>()>;

    // 在包 TU 的静态初始化期调用。
    static bool registerPack(const QString &id, PackFactory factory);

    // 全部已登记的包工厂（登记顺序）。
    static QList<QPair<QString, PackFactory>> packs();

    // 把全部已登记的包装载进给定注册表。返回装载成功的包 id。
    static QStringList installAll(ExtensionRegistry &registry);

    // 清空（仅测试用）。
    static void reset();
};

// 注册宏：在包 TU 文件末尾使用。
//
// 用法：
//   NG_REGISTER_PACK(ng::ext::AutopilotPack)
//
// 其中 AutopilotPack 需提供：
//   static QString packId();
//   static std::unique_ptr<ng::core::IToolPack> build();
//
// 宏必须带完整命名空间：它在调用处的命名空间作用域展开。
#define NG_REGISTER_PACK(FQN)                                                             \
    namespace {                                                                           \
    const bool g_ng_pack_reg_ =                                                           \
        ::ng::core::BuiltinPackRegistry::registerPack(                                    \
            FQN::packId(),                                                                \
            []() -> std::unique_ptr<::ng::core::IToolPack> { return FQN::build(); });    \
    }

} // namespace ng::core
