#include "InstantiationService.h"

#include <QString>
#include <QStringList>

#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
} // namespace

using nextpilot::plotscope::instantiation::InstantiationService;
using nextpilot::plotscope::instantiation::ServiceTypeTag;

// 两个带稳定 typeTag 的假服务类型（注册侧与取用侧共用同一个 value()）。
struct FakeWorkspace
{
    int x = 0;
};
struct FakeWindow
{
    int y = 0;
};

namespace nextpilot::plotscope::instantiation {
template <> struct ServiceTypeTag<FakeWorkspace>
{
    static QString value() { return QStringLiteral("vscode.workspace"); }
};
template <> struct ServiceTypeTag<FakeWindow>
{
    static QString value() { return QStringLiteral("vscode.window"); }
};
} // namespace nextpilot::plotscope::instantiation

int main()
{
    InstantiationService s;
    FakeWorkspace ws;
    FakeWindow win;
    int rogue = 9;

    // 注册 + 类型安全取用。
    s.registerSingleton("vscode.workspace", &ws, "assembly", 1, ServiceTypeTag<FakeWorkspace>::value());
    s.registerSingleton("vscode.window", &win, "assembly", 1, ServiceTypeTag<FakeWindow>::value());
    check(s.getService<FakeWorkspace>("vscode.workspace") == &ws, "typed get workspace");
    check(s.getService<FakeWindow>("vscode.window") == &win, "typed get window");
    check(s.getServiceUnchecked("vscode.workspace") == &ws, "unchecked get workspace");
    check(s.ids().size() == 2, "ids count");

    // typeTag 不符 → nullptr + 诊断（手填错误 tag 的条目）。
    s.registerSingleton("bad", &rogue, "assembly", 1, QStringLiteral("wrong.tag"));
    check(s.getService<FakeWorkspace>("bad") == nullptr, "typeTag mismatch -> nullptr");
    check(!s.diagnostics().isEmpty(), "mismatch diagnostic recorded");

    // 同 id 二次注册 → 拒绝、不覆盖、诊断。
    const int before = s.diagnostics().size();
    FakeWorkspace ws2;
    s.registerSingleton("vscode.workspace", &ws2, "intruder", 9, ServiceTypeTag<FakeWorkspace>::value());
    check(s.getService<FakeWorkspace>("vscode.workspace") == &ws, "duplicate did not overwrite");
    check(s.diagnostics().size() == before + 1, "duplicate diagnostic recorded");

    // 空 id / 空实例拒绝。
    const int before2 = s.diagnostics().size();
    s.registerSingleton(QString(), &rogue, "assembly", 1, "x");
    s.registerSingleton("nullinst", nullptr, "assembly", 1, "x");
    check(s.diagnostics().size() == before2 + 2, "empty id and null instance rejected");

    // 按 owner 倒序撤（后注册的先撤）。
    InstantiationService t;
    int a = 1, b = 2, c = 3;
    t.registerSingleton("a", &a, "owner1", 1, "x");
    t.registerSingleton("b", &b, "owner2", 1, "x");
    t.registerSingleton("c", &c, "owner1", 1, "x");
    t.unregisterOwner("owner1");
    check(t.getServiceUnchecked("a") == nullptr && t.getServiceUnchecked("c") == nullptr, "owner1 removed");
    check(t.getServiceUnchecked("b") == &b, "owner2 untouched");
    check(t.removedLog().size() == 2, "removed log count");
    check(t.removedLog().at(0) == QStringLiteral("c") && t.removedLog().at(1) == QStringLiteral("a"),
          "dependency reverse order (c before a)");

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ps_instantiation: all checks passed\n";
    return 0;
}
