#pragma once

#include <QString>
#include <QVector>

namespace ps::scripting
{
// 脚本的输入序列与派生输出；属脚本模块自有类型，不放中立层。
struct ScriptSeries
{
    QString name;
    QVector<double> time;
    QVector<double> value;
};

struct ScriptOutput
{
    QString name;
    QVector<double> time;
    QVector<double> value;
};

struct ScriptResult
{
    bool ok = false;
    QString error;
    QVector<ScriptOutput> outputs;
};
} // namespace ps::scripting
