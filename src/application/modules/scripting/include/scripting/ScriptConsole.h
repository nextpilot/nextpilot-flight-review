#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

struct lua_State;

namespace ps::scripting
{

// MATLAB 式控制台：一个持久 Lua workspace。
// 表达式回显结果并写入 `ans`；语句静默；print 的输出去控制台而不是 stdout。
class ScriptConsole
{
public:
    struct Result
    {
        enum Kind
        {
            Empty,  // 无输出（赋值、函数定义）
            Output, // 有文本要显示
            Clear,  // :clc
            Error,  // 执行失败，text 是错误
        };
        Kind kind = Empty;
        QString text;
    };

    ScriptConsole();
    ~ScriptConsole();

    ScriptConsole(const ScriptConsole &) = delete;
    ScriptConsole &operator=(const ScriptConsole &) = delete;

    // 执行一行（或多行拼接后的整段）。元命令以 `:` 开头。
    Result eval(const QString &input);

    // 语法未闭合（缺 end、括号未闭），调用方应继续收集下一条输入。
    bool incomplete(const QString &input) const;

    // 重建 workspace
    void clear();

    // 用户定义的全局名（排除内置与下划线内部名）
    QStringList globals() const;

    QString help() const;

    static bool isMetaCommand(const QString &input);

    // 宿主往 console state 里追加 API（结果面板等）；已建好 state 时立即生效，
    // clear() 重建时重新跑一遍。
    void addStateHook(std::function<void(lua_State *)> hook);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace ps::scripting
