// 统一变换 + 选择 + 能力防错契约测试（headless）。
//
// 钉死三件事：
//   1. Selection 统一模型（源/表/列/曲线/轴）可序列化、key 稳定；
//   2. TransformRegistry 按能力过滤，用户选不错；运行时再挡一次；
//   3. 三类变换真实求值：表达式走 Rust kernel，Lua 走 C++，单点/批量都正确。

#include "App.h"
#include "AppContext.h"
#include "Selection.h"
#include "Transform.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define NEAR(a, b) (std::fabs((a) - (b)) < 1e-6)

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

// 构造一份 TransformRequest JSON（字段赋值，避免依赖聚合初始化顺序）。
static QJsonObject makeReq(const QString &id, TransformKind kind,
                           const QString &expr, const QString &lua, const QJsonObject &params)
{
    TransformRequest r;
    r.descriptorId = id;
    r.kind = kind;
    r.expression = expr;
    r.luaSource = lua;
    r.params = params;
    return r.toJson();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ===== 1. Selection 统一模型 =====
    {
        Selection s;
        s.kind = SelectionKind::Column;
        s.label = QStringLiteral("att_roll");
        s.ids.insert(QStringLiteral("dataset"), 3);
        s.ids.insert(QStringLiteral("table"), QStringLiteral("att"));
        s.ids.insert(QStringLiteral("column"), QStringLiteral("roll"));
        CHECK(selectionKindName(s.kind) == QStringLiteral("column"));
        CHECK(selectionKindFromName(QStringLiteral("column")) == SelectionKind::Column);
        const QString k1 = s.key();
        const Selection copy = Selection::fromJson(s.toJson());
        CHECK(copy.kind == s.kind);
        CHECK(copy.ids.value(QStringLiteral("column")).toString() == QStringLiteral("roll"));
        CHECK(copy.key() == k1); // key 稳定：序列化往返不变
        Selection empty;
        CHECK(empty.empty() && empty.key().isEmpty());
        printf("OK  Selection 统一模型：kind 名称往返 / JSON 往返 / key 稳定\n");
    }

    // ===== 2. TransformRegistry 自描述 + 按能力过滤 =====
    {
        // 实例归组合根；测试里就地构造一份（构造即注册内置算子）。
        TransformRegistry transforms;
        const auto all = transforms.all();
        CHECK(all.size() >= 10); // 8 内置算子 + 表达式 + Lua
        for (const TransformDescriptor &d : all)
        {
            CHECK(!d.id.isEmpty());
            CHECK(!d.description.isEmpty()); // 每个都必须自说明
            CHECK(d.capabilities != CapNone); // 每个都必须声明能力
        }
        // Checksum 能力过滤：列出的都真支持 checksum，不支持的一律不出现。
        const auto checks = transforms.byCapability(CapChecksum);
        CHECK(!checks.isEmpty());
        for (const TransformDescriptor &d : checks)
            CHECK(d.supports(CapChecksum));
        bool hasNormalize = false;
        for (const TransformDescriptor &d : checks)
            if (d.id == QStringLiteral("builtin.normalize"))
                hasNormalize = true;
        CHECK(!hasNormalize); // normalize 需要全段，不支持 checksum 单点
        // "选错"在 registry 层就被过滤掉。
        CHECK(transforms.contains(QStringLiteral("expression")));
        CHECK(transforms.contains(QStringLiteral("lua.inline")));
        printf("OK  TransformRegistry：自描述齐全 / 按能力过滤 / 选错在 UI 层被挡\n");
    }

    // headless 启动，触发 script 模块注册 TransformEvaluator。
    App application;
    application.startup(true);

    // ===== 3. 表达式（Rust）：单点 + 批量 + 负号 =====
    {
        CHECK(application.context().interactions().hasTransformEvaluator());
        const QJsonObject req = makeReq(QStringLiteral("expression"), TransformKind::Expression,
                                        QStringLiteral("5*x+1"), QString(), QJsonObject());
        Result<QVector<double>> out{QVector<double>()};
        CHECK(application.context().interactions().evalTransform(req, CapAxisY,
              QVector<double>{1.0, 2.0, 3.0}, QVector<double>{1.0, 2.0, 3.0}, out));
        CHECK(out);
        CHECK(out.value().size() == 3);
        CHECK(NEAR(out.value().at(0), 6.0));
        CHECK(NEAR(out.value().at(1), 11.0));
        CHECK(NEAR(out.value().at(2), 16.0));
        printf("OK  表达式变换走 Rust：批量 5*x+1 正确\n");
    }

    // ===== 4. 内置算子（Rust）：比例/负号 =====
    {
        QJsonObject params;
        params.insert(QStringLiteral("value_scale"), -1.0);
        const QJsonObject req = makeReq(QStringLiteral("builtin.scale"), TransformKind::Builtin,
                                        QString(), QString(), params);
        Result<QVector<double>> out{QVector<double>()};
        CHECK(application.context().interactions().evalTransform(req, CapDerived,
              QVector<double>{0.0, 1.0}, QVector<double>{3.0, -4.0}, out));
        CHECK(out && out.value().size() == 2);
        CHECK(NEAR(out.value().at(0), -3.0));
        CHECK(NEAR(out.value().at(1), 4.0));
        printf("OK  内置算子走 Rust：比例取负正确\n");
    }

    // ===== 5. Lua（C++）：单点 + 批量 + ctx =====
    {
        QJsonObject lp; lp.insert(QStringLiteral("k"), 2.0);
        const QJsonObject req = makeReq(QStringLiteral("lua.inline"), TransformKind::Lua,
                                        QString(),
                                        QStringLiteral("function transform(x, ctx) return x*ctx.params.k + ctx.t end"),
                                        lp);
        Result<QVector<double>> out{QVector<double>()};
        CHECK(application.context().interactions().evalTransform(req, CapAxisY,
              QVector<double>{1.0, 2.0}, QVector<double>{10.0, 20.0}, out));
        CHECK(out && out.value().size() == 2);
        // t=0 默认（evalTransform 传的是给定 t 数组：这里 t=1,2）
        CHECK(NEAR(out.value().at(0), 21.0)); // 10*2 + 1
        CHECK(NEAR(out.value().at(1), 42.0)); // 20*2 + 2
        printf("OK  Lua 变换走 C++：批量 + ctx.params/ctx.t 正确\n");
    }

    // ===== 6. 能力防错：把只支持轴Y的算子用在 checksum 场景，运行时被挡 =====
    {
        const QJsonObject req = makeReq(QStringLiteral("builtin.normalize"), TransformKind::Builtin,
                                        QString(), QString(), QJsonObject());
        Result<QVector<double>> out{QVector<double>()};
        CHECK(application.context().interactions().evalTransform(req, CapChecksum,
              QVector<double>{0.0}, QVector<double>{1.0}, out));
        CHECK(!out); // 能力不匹配 -> 失败，"选错就死了"被挡住
        CHECK(out.error().code == QStringLiteral("ecap"));
        printf("OK  能力防错：normalize 用于 checksum 被运行时挡住（ecap）\n");
    }

    printf("\nALL TRANSFORM CONTRACT TESTS PASSED\n");
    return 0;
}
