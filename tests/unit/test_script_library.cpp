// 受限 require 验收：只认 <root>/<name>.lua，拒绝绝对路径/盘符/目录穿越。
#include "ScriptLibrary.h"
#include "scripting/ScriptEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>

namespace
{
bool writeFile(const QString &path, const char *text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(text);
    return true;
}

ScriptResult runScript(const QString &source)
{
    return ScriptEngine::run(source, QVector<ScriptSeries>(), {}, QString(), false);
}

bool fails(const QString &source)
{
    return !runScript(source).ok;
}
} // namespace

int main()
{
    QTemporaryDir tmp;
    assert(tmp.isValid());
    const QString lib = tmp.filePath(QStringLiteral("scripts/lib"));
    QDir().mkpath(lib);

    // 模块名由 chunk 参数传入，验证 `...` 可用
    assert(writeFile(lib + QStringLiteral("/roll_rate.lua"),
                     "local name = ...\n"
                     "return { name = name, half = function(v) return v * 0.5 end }\n"));
    assert(writeFile(lib + QStringLiteral("/dep.lua"), "return require(\"roll_rate\")\n"));
    assert(writeFile(lib + QStringLiteral("/counter.lua"),
                     "_G.__loads = (_G.__loads or 0) + 1\nreturn true\n"));
    assert(writeFile(lib + QStringLiteral("/a.lua"), "return require(\"b\")\n"));
    assert(writeFile(lib + QStringLiteral("/b.lua"), "return require(\"a\")\n"));
    assert(writeFile(lib + QStringLiteral("/broken.lua"), "error(\"boom\")\n"));

    ps::scripting::ScriptLibrary::setRoot(lib);
    assert(ps::scripting::ScriptLibrary::root() == QDir::cleanPath(lib));

    // 1. 正常加载 + chunk 参数
    {
        const ScriptResult r = runScript(QStringLiteral(
            "local m = require(\"roll_rate\")\n"
            "assert(m.name == \"roll_rate\")\n"
            "assert(m.half(4.0) == 2.0)\n"));
        if (!r.ok)
            std::fprintf(stderr, "basic failed: %s\n", qPrintable(r.error));
        assert(r.ok);
    }

    // 2. 跨模块复用（dep 内部再 require）
    {
        const ScriptResult r = runScript(QStringLiteral(
            "local d = require(\"dep\")\n"
            "assert(d.name == \"roll_rate\")\n"));
        assert(r.ok);
    }

    // 3. 同一 state 内二次 require 走缓存，不重复执行
    {
        const ScriptResult r = runScript(QStringLiteral(
            "require(\"counter\")\n"
            "require(\"counter\")\n"
            "assert(_G.__loads == 1)\n"));
        assert(r.ok);
    }

    // 4. 环依赖不死循环（加载前哨兵）
    {
        const ScriptResult r = runScript(QStringLiteral("require(\"a\")\n"));
        assert(r.ok);
    }

    // 5. 拒绝标准库名：package 已被摘，require 也找不到 os.lua
    assert(fails(QStringLiteral("require(\"os\")\n")));

    // 6. 拒绝目录穿越
    assert(fails(QStringLiteral("require(\"../../etc/passwd\")\n")));
    assert(fails(QStringLiteral("require(\"..\")\n")));

    // 7. 拒绝绝对路径与盘符
    assert(fails(QStringLiteral("require(\"/etc/passwd\")\n")));
    assert(fails(QStringLiteral("require(\"C:/Windows/win.ini\")\n")));
    assert(fails(QStringLiteral("require(\"sub\\\\x\")\n")));

    // 8. 不存在的模块
    assert(fails(QStringLiteral("require(\"nope\")\n")));

    // 9. 模块内报错向上传播
    {
        const ScriptResult r = runScript(QStringLiteral("require(\"broken\")\n"));
        assert(!r.ok);
        assert(r.error.contains(QStringLiteral("broken.lua")));
    }

    // 10. 失败后哨兵已清除：再次 require 仍失败，而不是假成功
    assert(fails(QStringLiteral("require(\"broken\")\n")));

    // 11. resolve 自身的名字校验
    QString path;
    QString error;
    assert(ps::scripting::ScriptLibrary::resolve(QStringLiteral("roll_rate"), &path, &error));
    assert(QFileInfo(path).fileName() == QStringLiteral("roll_rate.lua"));
    assert(!ps::scripting::ScriptLibrary::resolve(QStringLiteral("/abs"), &path, &error));
    assert(!ps::scripting::ScriptLibrary::resolve(QString(), &path, &error));

    std::printf("script library ok\n");
    return 0;
}
