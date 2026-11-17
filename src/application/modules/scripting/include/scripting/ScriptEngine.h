#pragma once

#include "ScriptService.h"

struct lua_State;

// 脚本只做编排：输入当前画布已有序列，输出新的派生序列。
// language 为空时按源码首行判断：`# python` / `#!python` 走 Python 3，其余走 Lua 5.4。

namespace ps::scripting
{
// 控制台用：建一个装好沙箱、计算库与数据面的持久 state（未启用 Lua 时返回 nullptr）。
lua_State *createConsoleState();
void destroyConsoleState(lua_State *L);
} // namespace ps::scripting

class ScriptEngine
{
public:
    // 失败时 outputs 为空，error 带行号与原因；不抛异常。
    // requireOutput：绘图派生脚本必须产出序列；控制台/自动化脚本允许裸跑。
    static ScriptResult run(const QString &source, const QVector<ScriptSeries> &inputs,
                            const QMap<QString, double> &params = QMap<QString, double>(),
                            const QString &language = QString(), bool requireOutput = true);
};
