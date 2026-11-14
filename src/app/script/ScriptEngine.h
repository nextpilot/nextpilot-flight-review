#pragma once

#include <QMap>
#include <QString>
#include <QVector>

// 脚本只做编排：输入当前画布已有序列，输出新的派生序列。
// language 为空时按源码首行判断：`# python` / `#!python` 走 Python 3，其余走 Lua 5.4。
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
    static ScriptResult run(const QString &source, const QVector<ScriptSeries> &inputs,
                            const QMap<QString, double> &params = QMap<QString, double>(),
                            const QString &language = QString());
};
