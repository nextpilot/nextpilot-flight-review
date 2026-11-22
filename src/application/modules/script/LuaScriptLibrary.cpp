#include "LuaScriptLibrary.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "ErrorCodes.h"
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::uischema;

namespace nextpilot::plotscope::script
{

namespace
{
// describe() 只用来构造元数据 table， forbid io/os/package/debug。
constexpr int kMaxDescribeInstructions = 200000;

void openDescribeLibs(lua_State *L)
{
    lua_pushcfunction(L, luaopen_base);
    lua_pushliteral(L, "_G");
    lua_call(L, 1, 0);
    lua_pushcfunction(L, luaopen_table);
    lua_pushliteral(L, "table");
    lua_call(L, 1, 0);
    lua_pushcfunction(L, luaopen_string);
    lua_pushliteral(L, "string");
    lua_call(L, 1, 0);
    lua_pushcfunction(L, luaopen_math);
    lua_pushliteral(L, "math");
    lua_call(L, 1, 0);
}

void interruptHook(lua_State *L, lua_Debug *)
{
    luaL_error(L, "describe() 执行步数超限");
}

QString luaError(lua_State *L)
{
    const char *err = lua_tostring(L, -1);
    const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本执行失败");
    lua_pop(L, 1);
    return msg;
}

// capabilities 字符串数组 -> 位标志。未知项忽略；空 = 全开。
uint32_t capabilitiesFromJson(const QJsonValue &v)
{
    uint32_t caps = CapNone;
    const QJsonArray arr = v.toArray();
    for (const QJsonValue &item : arr)
    {
        const QString s = item.toString().trimmed().toLower();
        if (s == QLatin1String("derived"))
            caps |= CapDerived;
        else if (s == QLatin1String("axisx"))
            caps |= CapAxisX;
        else if (s == QLatin1String("axisy"))
            caps |= CapAxisY;
        else if (s == QLatin1String("checksum"))
            caps |= CapChecksum;
    }
    return caps == CapNone ? CapTransform : caps;
}

TransformParam paramFromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    TransformParam p;
    p.key = o.value(QStringLiteral("key")).toString();
    p.label = o.value(QStringLiteral("label")).toString(p.key);
    p.type = o.value(QStringLiteral("type")).toString(QStringLiteral("number"));
    p.description = o.value(QStringLiteral("description")).toString();
    const QJsonValue def = o.contains(QStringLiteral("defaultValue"))
                               ? o.value(QStringLiteral("defaultValue"))
                               : o.value(QStringLiteral("default"));
    if (def.isBool())
        p.defaultValue = def.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    else if (def.isDouble())
        p.defaultValue = QString::number(def.toDouble(), 'g', 15);
    else
        p.defaultValue = def.toString();
    return p;
}

// 安全的脚本名：小写字母/数字/下划线/短横线，长度 1..64。
bool validScriptName(const QString &name)
{
    if (name.isEmpty() || name.size() > 64)
        return false;
    for (const QChar c : name)
        if (!(c.isLower() || c.isDigit() || c == QLatin1Char('_') || c == QLatin1Char('-')))
            return false;
    return true;
}
} // namespace

QJsonValue luaValueToJson(lua_State *L, int index)
{
    const int abs = lua_absindex(L, index);
    switch (lua_type(L, abs))
    {
    case LUA_TNIL:
        return QJsonValue::Null;
    case LUA_TBOOLEAN:
        return QJsonValue(lua_toboolean(L, abs) != 0);
    case LUA_TNUMBER:
        return QJsonValue(lua_tonumber(L, abs));
    case LUA_TSTRING:
    {
        size_t len = 0;
        const char *s = lua_tolstring(L, abs, &len);
        return QJsonValue(QString::fromUtf8(s, static_cast<int>(len)));
    }
    case LUA_TTABLE:
    {
        // 先扫一遍 key：全为 1..n 连续整数 → 数组；否则对象。
        bool isArray = true;
        int maxIndex = 0;
        int count = 0;
        lua_pushnil(L);
        while (lua_next(L, abs) != 0)
        {
            ++count;
            if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2))
            {
                const lua_Integer k = lua_tointeger(L, -2);
                if (k >= 1)
                    maxIndex = qMax(maxIndex, static_cast<int>(k));
                else
                    isArray = false;
            }
            else
            {
                isArray = false;
            }
            lua_pop(L, 1);
        }
        if (isArray && maxIndex != count)
            isArray = false;

        QJsonValue out;
        if (isArray)
        {
            QJsonArray arr;
            for (int i = 1; i <= maxIndex; ++i)
            {
                lua_rawgeti(L, abs, i);
                arr.append(luaValueToJson(L, -1));
                lua_pop(L, 1);
            }
            out = arr;
        }
        else
        {
            QJsonObject obj;
            lua_pushnil(L);
            while (lua_next(L, abs) != 0)
            {
                const QString key = luaValueToJson(L, -2).toString();
                if (!key.isEmpty())
                    obj.insert(key, luaValueToJson(L, -1));
                lua_pop(L, 1);
            }
            out = obj;
        }
        return out;
    }
    default:
        return QJsonValue::Null;
    }
}

LuaScriptLibrary::LuaScriptLibrary(TransformHub &transforms, QString opsDir)
    : m_transforms(transforms), m_opsDir(std::move(opsDir))
{
}

LuaScriptLibrary::~LuaScriptLibrary() = default;

QString LuaScriptLibrary::descriptorId(const QString &name) const
{
    return QStringLiteral("lua.%1").arg(name);
}

Result<TransformDescriptor> LuaScriptLibrary::describe(const QByteArray &source,
                                                       const QString &name) const
{
    lua_State *L = luaL_newstate();
    if (!L)
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));

    openDescribeLibs(L);
    lua_sethook(L, interruptHook, LUA_MASKCOUNT, kMaxDescribeInstructions);

    const QByteArray chunk = QByteArray("=@describe.") + name.toUtf8();
    if (luaL_loadbuffer(L, source.constData(), source.size(), chunk.constData()) != LUA_OK)
    {
        const QString err = luaError(L);
        lua_close(L);
        return fail(err::Syntax, err);
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        const QString err = luaError(L);
        lua_close(L);
        return fail(err::Runtime, err);
    }

    lua_getglobal(L, "describe");
    if (!lua_isfunction(L, -1))
    {
        lua_close(L);
        // 老脚本降级：文件名当 label、能力全开、不报错。
        TransformDescriptor d;
        d.id = descriptorId(name);
        d.label = name;
        d.kind = TransformKind::Lua;
        d.capabilities = CapTransform;
        d.description = QString();
        return Result<TransformDescriptor>(d);
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        const QString err = luaError(L);
        lua_close(L);
        return fail(err::Describe, err);
    }

    const QJsonObject meta = luaValueToJson(L, -1).toObject();
    lua_close(L);

    TransformDescriptor d;
    d.id = descriptorId(name);
    d.label = meta.value(QStringLiteral("label")).toString(name);
    d.kind = TransformKind::Lua;
    d.capabilities = capabilitiesFromJson(meta.value(QStringLiteral("capabilities")));
    d.description = meta.value(QStringLiteral("description")).toString();
    d.example = meta.value(QStringLiteral("example")).toString();
    const QJsonArray params = meta.value(QStringLiteral("params")).toArray();
    for (const QJsonValue &pv : params)
        d.params.append(paramFromJson(pv));
    return Result<TransformDescriptor>(d);
}

LuaScriptLibrary::LoadReport LuaScriptLibrary::loadAll()
{
    LoadReport report;
    QDir dir(m_opsDir);
    if (m_opsDir.isEmpty() || !dir.exists())
        return report;

    const QStringList files = dir.entryList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QString &file : files)
    {
        const QString name = QFileInfo(file).completeBaseName();
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly))
        {
            report.errors << QStringLiteral("%1: 读取失败").arg(name);
            continue;
        }
        const Result<TransformDescriptor> r = describe(f.readAll(), name);
        if (!r)
        {
            report.errors << QStringLiteral("%1: %2").arg(name, r.error().message);
            continue;
        }
        m_transforms.registerDescriptor(r.value());
        ++report.loaded;
    }
    return report;
}

Result<TransformDescriptor> LuaScriptLibrary::save(const QString &name, const QByteArray &source)
{
    if (!validScriptName(name))
        return fail(err::BadName,
                    QStringLiteral("脚本名只准小写字母/数字/下划线/短横线，长度 1..64"));

    const Result<TransformDescriptor> r = describe(source, name);
    if (!r)
        return r;

    if (m_opsDir.isEmpty() || !QDir().mkpath(m_opsDir))
        return fail(err::Io, QStringLiteral("无法创建脚本目录: %1").arg(m_opsDir));
    QFile f(QDir(m_opsDir).filePath(name + QStringLiteral(".lua")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(err::Io, QStringLiteral("无法写入脚本: %1").arg(f.fileName()));
    if (f.write(source) != source.size())
        return fail(err::Io, QStringLiteral("脚本写入不完整"));
    f.close();

    m_transforms.registerDescriptor(r.value());
    return r;
}

QStringList LuaScriptLibrary::list() const
{
    if (m_opsDir.isEmpty())
        return {};
    QDir dir(m_opsDir);
    if (!dir.exists())
        return {};
    QStringList names;
    const QStringList files = dir.entryList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QString &file : files)
        names << QFileInfo(file).completeBaseName();
    return names;
}

Result<void> LuaScriptLibrary::remove(const QString &name)
{
    if (!validScriptName(name))
        return fail(err::BadName,
                    QStringLiteral("脚本名只准小写字母/数字/下划线/短横线，长度 1..64"));
    const QString path = QDir(m_opsDir).filePath(name + QStringLiteral(".lua"));
    QFile f(path);
    if (!f.exists())
        return fail(err::NoEnt, QStringLiteral("脚本不存在: %1").arg(name));
    if (!f.remove())
        return fail(err::Io, QStringLiteral("无法删除脚本: %1").arg(path));
    // descriptor 与文件同名（save/loadAll 均如此），同步摘除保持注册表一致。
    m_transforms.removeDescriptor(descriptorId(name));
    return Result<void>();
}

QByteArray LuaScriptLibrary::resolveSource(const QString &scriptId,
                                           const QString &snapshot) const
{
    if (scriptId.isEmpty() || m_opsDir.isEmpty())
        return snapshot.toUtf8();

    if (!validScriptName(scriptId))
        return snapshot.toUtf8();

    QFile f(QDir(m_opsDir).filePath(scriptId + QStringLiteral(".lua")));
    if (!f.open(QIODevice::ReadOnly))
        return snapshot.toUtf8(); // 文件缺失用快照兜底。

    return f.readAll();
}

} // namespace nextpilot::plotscope::script
