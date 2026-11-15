#pragma once

#include <QMap>
#include <functional>
#include <QString>
#include <QVector>

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

struct ScriptRunnerContribution
{
    std::function<ScriptResult(const QString &source, const QVector<ScriptSeries> &inputs,
                               const QMap<QString, double> &params, const QString &language)>
        run;
};
