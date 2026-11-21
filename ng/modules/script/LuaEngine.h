#pragma once

#include "ScriptFacade.h"
#include "Result.h"

#include <QJsonObject>
#include <QVariant>
#include <QVector>
#include <QString>

#include <functional>

struct lua_State;

namespace nextpilot::plotscope::script
{

// Lua 引擎：封装 Lua VM，把 ScriptFacade 注入为全局对象 app（ps 为兼容别名）。
// 脚本可经 app.call(op, params) 操作整个应用（意图 + view action 智能路由），
// 类似 CLI；但拿不到 QWidget 或模块内部指针，仍保持 headless-safe。
class LuaEngine
{
public:
    // 输出 sink：print / app.log 的去向。设置后进 sink（REPL 控制台），
    // 不设保持旧行为（stdout/stderr，MCP stdio 桥兼容）。
    using OutputSink = std::function<void(const QString &)>;
    void setOutputSink(OutputSink sink) { m_sink = std::move(sink); }
    // C 回调（print/app.log）经这两个方法路由；脚本本身拿不到引擎指针。
    bool hasOutputSink() const { return static_cast<bool>(m_sink); }
    void emitToSink(const QString &msg) { if (m_sink) m_sink(msg); }

    explicit LuaEngine(nextpilot::plotscope::context::ScriptFacade &facade);
    ~LuaEngine();

    LuaEngine(const LuaEngine &) = delete;
    LuaEngine &operator=(const LuaEngine &) = delete;

    // 执行一段脚本源码；失败时 Result 带 Lua 错误信息。
    nextpilot::plotscope::foundation::Result<void> runString(const QByteArray &source, const QString &chunkName = QStringLiteral("=script"));

    // 执行脚本文件。
    nextpilot::plotscope::foundation::Result<void> runFile(const QString &path);

    // REPL 求值：先试 `return (expr)`，编译不过降级为整段语句。
    // 与 runString 共用同一 VM —— 连续求值共享全局状态。
    // 错误信息带 repl:<行号> 前缀，控制台可点击跳转。
    nextpilot::plotscope::foundation::Result<QVariant> evaluate(const QString &source);

private:
    lua_State *m_lua = nullptr;
    OutputSink m_sink;
};

// ===== Lua 变换求值（单点 live / 批量 offline 共用）=====
//
// 用途：checksum、曲线轴变换、数据源衍生里"用户写的 Lua 代码"。
// 约定：源码定义全局函数 `function transform(x, ctx) return y end`。
//   · x     —— 当前点的输入值（Y 变换=值；X 变换=时间秒；checksum=字段值/字节）；
//   · ctx   —— 上下文 table：ctx.t（时间秒）、ctx.index（点序号）、
//              ctx.params（参数 table，来自 descriptor.params）。
// 也兼容"源码就是表达式"：若未定义 transform，则把源码包成 `return (<src>)`，
// 其中 x/ctx 为全局变量——方便在线调试时随手写 `x*2+1`。
//
// 执行位置：C++（用户 3 次澄清后定稿：Lua/表达式在曲线/checksum 侧走 C++，
// 便于在线调试单点/批量；数据源侧的算子与数学表达式走 Rust）。
class LuaTransform
{
public:
    explicit LuaTransform(const QByteArray &source, const QJsonObject &params = QJsonObject(),
                          const QString &name = QStringLiteral("=transform"));
    ~LuaTransform();

    LuaTransform(const LuaTransform &) = delete;
    LuaTransform &operator=(const LuaTransform &) = delete;

    bool valid() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }

    // 单点求值（live 数据源 / checksum / 在线调试单点输入）。
    bool eval(double x, double t, int index, double *out, QString *err = nullptr) const;

    // 批量求值（offline 数据源 / 在线调试批量输入）。输入 t/v 等长，输出同长。
    // 直接在曲线/属性面板侧调用；不写回任何 Dataset。
    bool evalSeries(const QVector<double> &t, const QVector<double> &v,
                    QVector<double> *out, QString *err = nullptr) const;

private:
    lua_State *m_lua = nullptr;
    QJsonObject m_params;
    QString m_error;
};

} // namespace nextpilot::plotscope::script
