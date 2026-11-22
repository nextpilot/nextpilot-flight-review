// 有状态 Lua 过滤器引擎 headless 验收。
// 覆盖：descriptor 读取（不实例化）、有状态 calculate/reset、nil 抑制、
// (t,v) 显式时间、sticky 失败、看门狗、内置过滤器 golden 行为。
#include "FilterEngine.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <memory>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::script;

namespace
{
int g_fail = 0;
#define CHECK(cond)                                                                     \
    do                                                                                  \
    {                                                                                   \
        if (!(cond))                                                                    \
        {                                                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_fail;                                                                   \
        }                                                                               \
    } while (0)

// 一个多类模块：derivative（有状态+抑制）+ scale（参数+显式时间）+ boom（错误）。
const char *kModule = R"LUA(
local Derivative = {
  id = "derivative", name = "Derivative", description = "dv/dt",
  parameters = { {name="custom_dt", type="number", default=0.0, label="Fixed dt", min=0.0} },
}
Derivative.__index = Derivative
function Derivative.create(p) return setmetatable({fixed=p.custom_dt or 0.0, has=false, pt=0, pv=0}, Derivative) end
function Derivative:reset() self.has=false end
function Derivative:calculate(t, v)
  if not self.has then self.has,self.pt,self.pv=t,t,v; return nil end
  local dt = self.fixed>0 and self.fixed or (t-self.pt)
  local d = dt~=0 and (v-self.pv)/dt or 0
  self.pt,self.pv=t,v
  return d
end

local Scale = { id="scale", name="Scale", description="affine+shift", output="same",
  parameters = {
    {name="k", type="number", default=2.0, label="Scale"},
    {name="b", type="number", default=1.0, label="Offset"},
    {name="dt", type="number", default=0.5, label="Time shift"},
  },
}
Scale.__index = Scale
function Scale.create(p) return setmetatable({k=p.k,b=p.b,dt=p.dt}, Scale) end
function Scale:calculate(t,v) return t+self.dt, self.k*v+self.b end

local Boom = { id="boom", name="Boom" }
Boom.__index = Boom
function Boom.create(_) return setmetatable({}, Boom) end
function Boom:calculate(_,_) error("boom!") end

local Spin = { id="spin", name="Spin" }
Spin.__index = Spin
function Spin.create(_) return setmetatable({}, Spin) end
function Spin:calculate(_,_) while true do end end

return { Derivative, Scale, Boom, Spin }
)LUA";

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto engine = makeLuaFilterEngine();

    // --- 1. inspectModule：读 descriptor，不实例化 ---
    const auto inspected = engine->inspectModule(QByteArray(kModule), QStringLiteral("test"));
    CHECK(static_cast<bool>(inspected));
    CHECK(inspected.value().size() == 4);
    const FilterDescriptor *derivDesc = nullptr;
    const FilterDescriptor *scaleDesc = nullptr;
    for (const auto &d : inspected.value())
    {
        if (d.id == QStringLiteral("derivative")) derivDesc = &d;
        if (d.id == QStringLiteral("scale")) scaleDesc = &d;
    }
    CHECK(derivDesc != nullptr);
    CHECK(derivDesc->name == QStringLiteral("Derivative"));
    CHECK(derivDesc->parameters.size() == 1);
    CHECK(derivDesc->parameters[0].name == QStringLiteral("custom_dt"));
    CHECK(derivDesc->parameters[0].type == FilterParamType::Number);
    CHECK(derivDesc->parameters[0].hasMin);
    CHECK(derivDesc->parameters[0].defaultValue.toDouble() == 0.0);
    CHECK(scaleDesc != nullptr);
    CHECK(scaleDesc->outputKind == QStringLiteral("same"));
    CHECK(scaleDesc->parameters.size() == 3);

    // --- 2. derivative：首样本抑制 + 之后 dv/dt ---
    QJsonObject derivParams;
    auto dInstR = engine->createInstance(*derivDesc, derivParams);
    CHECK(static_cast<bool>(dInstR));
    auto dInst = std::move(dInstR.value());
    FilterSampleResult r0 = dInst->calculate(0.0, 10.0);
    CHECK(r0.suppress); // 首样本抑制
    FilterSampleResult r1 = dInst->calculate(1.0, 30.0); // (30-10)/(1-0)=20
    CHECK(!r1.suppress);
    CHECK(std::abs(r1.value - 20.0) < 1e-9);
    // reset 后重新抑制
    dInst->reset();
    FilterSampleResult r2 = dInst->calculate(2.0, 100.0);
    CHECK(r2.suppress);

    // --- 3. scale：显式时间 (t+dt, k*v+b) ---
    QJsonObject sp;
    sp.insert(QStringLiteral("k"), 3.0);
    sp.insert(QStringLiteral("b"), 2.0);
    sp.insert(QStringLiteral("dt"), 0.25);
    auto sInstR = engine->createInstance(*scaleDesc, sp);
    CHECK(static_cast<bool>(sInstR));
    auto sInst = std::move(sInstR.value());
    FilterSampleResult sr = sInst->calculate(5.0, 4.0);
    CHECK(!sr.suppress);
    CHECK(sr.hasExplicitTime);
    CHECK(std::abs(sr.outTime - 5.25) < 1e-9);
    CHECK(std::abs(sr.value - 14.0) < 1e-9);

    // --- 4. 默认参数：create 不传 params 时用 descriptor default ---
    auto dDefaultR = engine->createInstance(*scaleDesc, QJsonObject());
    CHECK(static_cast<bool>(dDefaultR));
    auto dDefault = std::move(dDefaultR.value());
    FilterSampleResult dr = dDefault->calculate(1.0, 2.0);
    // k=2,b=1 → 5；dt=0.5 → 1.5
    CHECK(std::abs(dr.value - 5.0) < 1e-9);
    CHECK(std::abs(dr.outTime - 1.5) < 1e-9);

    // --- 5. sticky 失败：boom 报错后一直失败 ---
    const FilterDescriptor *boomDesc = nullptr;
    for (const auto &d : inspected.value())
        if (d.id == QStringLiteral("boom")) boomDesc = &d;
    CHECK(boomDesc != nullptr);
    auto bInstR = engine->createInstance(*boomDesc, QJsonObject());
    CHECK(static_cast<bool>(bInstR));
    auto bInst = std::move(bInstR.value());
    FilterSampleResult br = bInst->calculate(0.0, 0.0);
    CHECK(br.suppress);
    CHECK(bInst->failed());
    CHECK(bInst->error().contains(QStringLiteral("boom")));
    FilterSampleResult br2 = bInst->calculate(1.0, 1.0);
    CHECK(br2.suppress); // 仍失败

    // --- 6. 看门狗：spin 死循环被中断（sticky 失败） ---
    const FilterDescriptor *spinDesc = nullptr;
    for (const auto &d : inspected.value())
        if (d.id == QStringLiteral("spin")) spinDesc = &d;
    CHECK(spinDesc != nullptr);
    auto s2R = engine->createInstance(*spinDesc, QJsonObject());
    CHECK(static_cast<bool>(s2R));
    auto s2 = std::move(s2R.value());
    FilterSampleResult spinR = s2->calculate(0.0, 0.0);
    CHECK(spinR.suppress);
    CHECK(s2->failed()); // 预算耗尽被中断
    CHECK(s2->error().contains(QStringLiteral("budget")));

    if (g_fail == 0)
        std::printf("test_filter_engine: ALL PASS\n");
    else
        std::fprintf(stderr, "test_filter_engine: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
