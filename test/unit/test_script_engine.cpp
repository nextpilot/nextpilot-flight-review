#include "ScriptEngine.h"

#include <cassert>
#include <cmath>

static ScriptSeries makeSeries(const QString &name, const QVector<double> &t,
                               const QVector<double> &v)
{
    ScriptSeries s;
    s.name = name;
    s.time = t;
    s.value = v;
    return s;
}

int main()
{
    const QVector<double> t{0.0, 0.1, 0.2};
    const QVector<double> a{1.0, 2.0, 3.0};
    const QVector<double> b{4.0, 5.0, 6.0};
    const QVector<ScriptSeries> inputs{makeSeries("v1", t, a), makeSeries("v2", t, b)};

    const ScriptResult ok = ScriptEngine::run(
        QStringLiteral("local x = ps.input(1)\n"
                       "local y = ps.input(2)\n"
                       "assert(#x == 3 and #y == 3)\n"
                       "ps.plot(\"sum\", ps.scale(x, 1.0, y[1]))\n"),
        inputs);
    assert(ok.ok);
    assert(ok.outputs.size() == 1);
    assert(ok.outputs.first().name == "sum");
    assert(ok.outputs.first().value.size() == 3);
    assert(std::abs(ok.outputs.first().value[0] - 5.0) < 1e-9);

    const ScriptResult step = ScriptEngine::run(
        QStringLiteral("local acc = 0\n"
                       "local k = 1\n"
                       "function init(params) acc = 0 k = params.k or 1 end\n"
                       "function reset() acc = 0 end\n"
                       "function step(t, v1, v2) acc = acc + v1 * k return {sum = acc} end\n"),
        inputs, {{QStringLiteral("k"), 2.0}});
    assert(step.ok);
    assert(step.outputs.size() == 1);
    assert(step.outputs.first().name == "sum");
    assert(step.outputs.first().value.size() == 3);
    assert(std::abs(step.outputs.first().value[2] - 12.0) < 1e-9);

    const ScriptResult py = ScriptEngine::run(
        QStringLiteral("# python\n"
                       "import json,sys\n"
                       "d=json.load(sys.stdin)\n"
                       "v=d['inputs'][0]['value']\n"
                       "t=d['inputs'][0]['time']\n"
                       "json.dump({'outputs':[{'name':'py_sum','time':t,'value':[x*2 for x in v]}]},sys.stdout)\n"),
        inputs);
    assert(py.ok);
    assert(py.outputs.size() == 1);
    assert(std::abs(py.outputs.first().value[2] - 6.0) < 1e-9);

    const ScriptResult bad = ScriptEngine::run(QStringLiteral("ps.input(5)"), inputs);
    assert(!bad.ok);
    assert(bad.error.contains(QStringLiteral("1..4")));
    return 0;
}
