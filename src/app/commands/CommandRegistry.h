#pragma once

#include <QString>
#include <functional>
#include <map>

class QWidget;

namespace ps::commands
{

struct CommandContext
{
    QWidget *parent = nullptr;
    QString arg;
};

class CommandRegistry
{
public:
    using Handler = std::function<void(const CommandContext &)>;

    void add(const QString &id, Handler handler);
    bool contains(const QString &id) const;
    bool invoke(const QString &id, const CommandContext &ctx) const;

private:
    std::map<QString, Handler> m_handlers;
};

} // namespace ps::commands
