// 报告模板贡献独立验收（M2.3 2.3.1）：只链 ng_registry + Qt6::Core。
// 验证：ReportTemplate 纯数据零虚函数；扩展包可贡献模板并 mount/unmount；
// provider/domain 由 mount() 自动写入；按包原子撤销。

#include "Contribution.h"
#include "ExtensionRegistry.h"

#include <QCoreApplication>

#include <cstdio>
#include <type_traits>

using namespace nextpilot::plotscope::registry;

namespace
{
int g_failed = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // --- 0. 纯数据纪律：零虚函数、可拷贝 ---
    CHECK(!std::is_polymorphic_v<ReportTemplate>);
    CHECK(!std::is_polymorphic_v<ReportSection>);
    CHECK(std::is_copy_constructible_v<ReportTemplate>);

    // --- 1. 扩展包贡献模板 + mount 自动写归属 ---
    ExtensionRegistry reg;
    reg.installBuiltinPacks(); // 内置包先挂上（含 id 占位，防撞）

    Pack pack;
    pack.id = QStringLiteral("pack.report.test");
    pack.name = QStringLiteral("报告测试包");
    pack.domain = Domain::Autopilot;

    ReportTemplate tpl;
    tpl.id = QStringLiteral("report.flight-check");
    tpl.title = QStringLiteral("飞行检查报告");
    tpl.description = QStringLiteral("飞控调参检查清单报告");
    tpl.sections = {
        {QStringLiteral("title"), {}, QStringLiteral("飞行数据检查报告")},
        {QStringLiteral("metadata"), {}, QStringLiteral("time,file,dataset")},
        {QStringLiteral("canvas"), QStringLiteral("姿态曲线"), {}},
        {QStringLiteral("text"), QStringLiteral("结论"), QStringLiteral("振动正常，EKF 收敛。")},
    };
    pack.reports.append(tpl);
    CHECK(reg.mount(std::move(pack)));

    const ReportTemplate *got = reg.reports.find(QStringLiteral("report.flight-check"));
    CHECK(got != nullptr);
    if (got)
    {
        CHECK(got->provider == QStringLiteral("pack.report.test"));
        CHECK(got->domain == Domain::Autopilot);
        CHECK(got->sections.size() == 4);
        CHECK(got->sections.at(0).kind == QStringLiteral("title"));
        CHECK(got->sections.at(2).kind == QStringLiteral("canvas"));
    }

    // --- 2. 重复 id 挂载被拒 ---
    {
        Pack dup;
        dup.id = QStringLiteral("pack.report.test2");
        ReportTemplate t2;
        t2.id = QStringLiteral("report.flight-check");
        dup.reports.append(t2);
        CHECK(!reg.mount(std::move(dup)));
    }

    // --- 3. 按包原子卸载：模板随包消失，其他包不受牵连 ---
    const int packsBefore = reg.packs.count();
    CHECK(reg.unmount(QStringLiteral("pack.report.test")));
    CHECK(reg.reports.find(QStringLiteral("report.flight-check")) == nullptr);
    CHECK(reg.packs.count() == packsBefore - 1);
    CHECK(!reg.unmount(QStringLiteral("pack.report.test"))); // 二次卸载 false

    if (g_failed == 0)
    {
        std::printf("report_template: ALL PASS\n");
        return 0;
    }
    std::printf("report_template: %d FAILED\n", g_failed);
    return 1;
}
