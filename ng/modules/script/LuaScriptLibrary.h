#pragma once

#include "Result.h"
#include "Transform.h"

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

struct lua_State;

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::uischema::TransformDescriptor;

// Lua 脚本库：扫描目录、沙箱执行 describe()、注册 descriptor、保存脚本、解析源码。
// 有状态对象，由组合根构造并注入；不进 libs。
class LuaScriptLibrary
{
public:
    explicit LuaScriptLibrary(nextpilot::plotscope::uischema::TransformRegistry &transforms,
                              QString opsDir = QString());
    ~LuaScriptLibrary();

    LuaScriptLibrary(const LuaScriptLibrary &) = delete;
    LuaScriptLibrary &operator=(const LuaScriptLibrary &) = delete;

    const QString &opsDir() const { return m_opsDir; }
    void setOpsDir(const QString &dir) { m_opsDir = dir; }

    // 扫描 opsDir 下 *.lua，逐个 describe() 并注册 lua.<name>。返回失败明细。
    struct LoadReport
    {
        int loaded = 0;
        QStringList errors;
    };
    LoadReport loadAll();

    // 解析脚本源码的 describe()，产出 descriptor（id = lua.<name>）。
    // 无 describe() 的脚本降级：文件名当 label、能力全开、不报错。
    Result<TransformDescriptor> describe(const QByteArray &source, const QString &name) const;

    // 保存脚本到 opsDir/<name>.lua，describe 后注册 descriptor。name 只准安全字符。
    Result<TransformDescriptor> save(const QString &name, const QByteArray &source);

    // 列出脚本库里的脚本名（不含 .lua 后缀，按名称排序）。目录不存在返回空。
    QStringList list() const;

    // 删除脚本文件并同步摘除 descriptor。name 为空/非法/不存在均报错。
    Result<void> remove(const QString &name);

    // 解析请求的 Lua 源码：优先文件（scriptId → opsDir/<scriptId>.lua），缺失用快照兜底。
    // 返回最终要喂给求值器的源码；outRefresh 给调用方回填快照。
    QByteArray resolveSource(const QString &scriptId, const QString &snapshot) const;

    QString descriptorId(const QString &name) const;

private:
    nextpilot::plotscope::uischema::TransformRegistry &m_transforms; // 注册表由组合根注入
    QString m_opsDir;
};

// Lua table → QJsonValue，供 describe() 元数据转换和测试复用。
// 数组判定：连续整数 key 1..n；其余按对象处理。
QJsonValue luaValueToJson(struct lua_State *L, int index);

} // namespace nextpilot::plotscope::script
