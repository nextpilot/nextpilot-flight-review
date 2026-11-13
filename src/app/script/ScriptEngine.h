#pragma once

#include <QString>
#include <QVector>

// Lua 只做编排：输入当前画布已有序列，输出新的派生序列。
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

class ScriptEngine
{
public:
    // 失败时 outputs 为空，error 带行号与原因；不抛异常。
    static ScriptResult run(const QString &source, const QVector<ScriptSeries> &inputs);
};
