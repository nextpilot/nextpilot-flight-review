#pragma once

#include <QString>

namespace ps::commands
{

// 命令总线最小契约：runtime/模块只依赖“按 ID 执行”，不依赖 shell 的
// CommandManager、QWidget 或快捷键绑定。shell 的 CommandManager 实现本接口。
class CommandBus
{
public:
    virtual ~CommandBus() = default;
    virtual bool contains(const QString &id) const = 0;
    virtual bool invoke(const QString &id, const QString &arg) = 0;
};

} // namespace ps::commands
