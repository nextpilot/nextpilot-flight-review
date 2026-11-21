// B8-a：Lua 脚本库 describe() 加载器 headless 验收。
// 沙箱解析元数据、table->QJson 转换、老脚本降级、保存注册、scriptId 快照兜底。
#include "LuaScriptLibrary.h"
#include "Transform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonObject>

#include <cstdio>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::script;

namespace
{
int g_fail = 0;
#define CHECK(cond)                                                                     \
    do                                                                                  \
    {                                                                                   \
        if (!(cond))                                                                    \
        {                                                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_fail;                                                                   \
        }                                                                               \
    } while (0)

QByteArray scriptWithDescribe()
{
    return R"lua(
function describe()
  return {
    label = "增益缩放",
    description = "把输入乘以一个系数",
    capabilities = { "derived", "axisY" },
    example = "function transform(x, ctx) return x * 2 end",
    params = {
      { key = "gain", label = "增益", type = "number", default = 2.5 },
      { key = "enabled", type = "bool", defaultValue = true },
    },
  }
end
function transform(x, ctx)
  return x * (ctx.params.gain or 1)
end
)lua";
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 每用例一份实例（构造即含内置算子）；两份库共享同一张注册表。
    TransformRegistry transforms;
    transforms.clear();

    // --- 1. describe() 元数据解析 ---
    LuaScriptLibrary lib(transforms);
    const Result<TransformDescriptor> r = lib.describe(scriptWithDescribe(), QStringLiteral("gain"));
    CHECK(r.ok());
    if (r)
    {
        const TransformDescriptor &d = r.value();
        CHECK(d.id == QStringLiteral("lua.gain"));
        CHECK(d.label == QStringLiteral("增益缩放"));
        CHECK(d.description == QStringLiteral("把输入乘以一个系数"));
        CHECK(d.kind == TransformKind::Lua);
        CHECK(d.capabilities == (CapDerived | CapAxisY)); // 指定值不扩展成全开
        CHECK(d.params.size() == 2);
        if (d.params.size() == 2)
        {
            CHECK(d.params.at(0).key == QStringLiteral("gain"));
            CHECK(d.params.at(0).label == QStringLiteral("增益"));
            CHECK(d.params.at(0).type == QStringLiteral("number"));
            CHECK(d.params.at(0).defaultValue == QStringLiteral("2.5"));
            CHECK(d.params.at(1).type == QStringLiteral("bool"));
            CHECK(d.params.at(1).defaultValue == QStringLiteral("true"));
        }
    }

    // --- 2. 无 describe() 老脚本降级：不报错、文件名当 label、能力全开 ---
    const Result<TransformDescriptor> old = lib.describe(
        QByteArray("function transform(x, ctx) return x end\n"), QStringLiteral("legacy"));
    CHECK(old.ok());
    if (old)
    {
        CHECK(old.value().label == QStringLiteral("legacy"));
        CHECK(old.value().capabilities == CapTransform);
        CHECK(old.value().id == QStringLiteral("lua.legacy"));
    }

    // --- 3. 语法错误报 esyntax，不崩 ---
    const Result<TransformDescriptor> bad = lib.describe(
        QByteArray("function describe( ???\n"), QStringLiteral("broken"));
    CHECK(!bad.ok());
    CHECK(bad.error().code == QStringLiteral("esyntax"));

    // --- 4. describe() 里死循环被指令钩子截断 ---
    const Result<TransformDescriptor> loop = lib.describe(
        QByteArray("function describe() while true do end end\n"), QStringLiteral("loop"));
    CHECK(!loop.ok());
    CHECK(loop.error().code == QStringLiteral("edescribe"));

    // --- 5. 沙箱：describe 里碰 io 应失败（io 库未开放）---
    const Result<TransformDescriptor> sandbox = lib.describe(
        QByteArray("function describe() io.open('/etc/passwd') return {} end\n"),
        QStringLiteral("sandbox"));
    CHECK(!sandbox.ok());

    // --- 6. 保存到临时目录 + 注册 + resolveSource 文件优先 ---
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    LuaScriptLibrary fileLib(transforms, tmp.path());
    const Result<TransformDescriptor> saved = fileLib.save(
        QStringLiteral("saved_gain"), scriptWithDescribe());
    CHECK(saved.ok());
    CHECK(transforms.contains(QStringLiteral("lua.saved_gain")));
    CHECK(QFile::exists(QDir(tmp.path()).filePath(QStringLiteral("saved_gain.lua"))));

    // 文件为真源：快照写错，resolveSource 应返回文件里的正确源码。
    const QByteArray resolved = fileLib.resolveSource(
        QStringLiteral("saved_gain"), QStringLiteral("-- wrong snapshot"));
    CHECK(resolved.contains("function describe()"));
    CHECK(!resolved.contains("wrong snapshot"));

    // 文件缺失：用快照兜底。
    const QByteArray fallback = fileLib.resolveSource(
        QStringLiteral("missing_script"), QStringLiteral("return 42"));
    CHECK(fallback == QByteArray("return 42"));

    // --- 7. loadAll 扫描目录并注册 ---
    transforms.clear();
    const LuaScriptLibrary::LoadReport report = fileLib.loadAll();
    CHECK(report.loaded == 1);
    CHECK(report.errors.isEmpty());
    CHECK(transforms.contains(QStringLiteral("lua.saved_gain")));

    // --- 8. 非法脚本名被拒 ---
    const Result<TransformDescriptor> invalid = fileLib.save(
        QStringLiteral("../escape"), scriptWithDescribe());
    CHECK(!invalid.ok());
    CHECK(invalid.error().code == QStringLiteral("ebadname"));

    // --- 9. list()：目录列举（不含后缀、按名称排序） ---
    fileLib.save(QStringLiteral("alpha"), scriptWithDescribe());
    const QStringList listed = fileLib.list();
    CHECK(listed.contains(QStringLiteral("saved_gain")));
    CHECK(listed.contains(QStringLiteral("alpha")));
    CHECK((listed == QStringList{QStringLiteral("alpha"), QStringLiteral("saved_gain")}));

    // --- 10. remove()：删文件 + 同步摘除 descriptor ---
    const Result<void> removed = fileLib.remove(QStringLiteral("alpha"));
    CHECK(removed.ok());
    CHECK(!QFile::exists(QDir(tmp.path()).filePath(QStringLiteral("alpha.lua"))));
    CHECK(!transforms.contains(QStringLiteral("lua.alpha")));
    CHECK(!fileLib.list().contains(QStringLiteral("alpha")));
    // 不存在/非法名各给明确报错
    const Result<void> missing = fileLib.remove(QStringLiteral("alpha"));
    CHECK(!missing.ok());
    CHECK(missing.error().code == QStringLiteral("enoent"));
    const Result<void> badName = fileLib.remove(QStringLiteral("../x"));
    CHECK(!badName.ok());
    CHECK(badName.error().code == QStringLiteral("ebadname"));

    if (g_fail == 0)
        std::printf("test_lua_script_library: ALL PASS\n");
    else
        std::fprintf(stderr, "test_lua_script_library: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
