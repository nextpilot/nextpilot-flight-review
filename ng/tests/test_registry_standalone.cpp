// registry 独立验收：只链 ng_registry + Qt6::Core，不链 ng_core。
// 验证泛型 CRUD、自注册宏、工厂型条目。

#include "Registry.h"
#include "StaticAutoRegistry.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <functional>
#include <string>

using nextpilot::plotscope::registry::Registry;
using nextpilot::plotscope::registry::StaticRegistry;

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

// 测试用的条目类型。
struct Ping
{
    int value;
};

// 启动期自注册，验证宏在 main 前已登记。
NG_AUTO_REGISTER(Ping, probe, QStringLiteral("builtin"), Ping{42})

// 另一套表，验证不同条目类型互不串表。
using StringRegistry = Registry<std::string>;
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // --- 泛型 CRUD ---
    StringRegistry reg;
    CHECK(reg.isEmpty());
    CHECK(reg.count() == 0);
    CHECK(reg.insert("alpha", std::string("A")));
    CHECK(reg.insert("beta", std::string("B")));
    CHECK(!reg.insert("alpha", std::string("dup"))); // 重复 id：先到者为准
    CHECK(reg.count() == 2);
    CHECK(reg.contains("alpha"));
    CHECK(!reg.contains("missing"));
    CHECK(reg.find("alpha") != nullptr);
    CHECK(*reg.find("alpha") == "A");
    CHECK(reg.find("missing") == nullptr);
    CHECK(reg.value("alpha") == "A");                    // 值型：缺键返回默认构造
    CHECK(reg.value("missing", std::string("D")) == "D"); // 指针条目用 value 免解引用
    Registry<int *> ptrReg;
    int x = 7;
    CHECK(ptrReg.insert("p", &x));
    CHECK(ptrReg.value("p", nullptr) == &x);
    CHECK(ptrReg.value("none", nullptr) == nullptr);

    auto keys = reg.keys();
    keys.sort();
    CHECK(keys.size() == 2);
    CHECK(keys.at(0) == "alpha");
    CHECK(keys.at(1) == "beta");

    CHECK(reg.remove("alpha"));
    CHECK(!reg.remove("alpha")); // 二次移除返回 false
    CHECK(reg.count() == 1);
    reg.clear();
    CHECK(reg.isEmpty());

    // --- 自注册助手：宏登记的条目在 main 前已就位 ---
    auto &builtins = StaticRegistry<Ping>::table;
    CHECK(builtins.count() == 1);
    CHECK(builtins.contains(QStringLiteral("builtin")));
    auto *ping = builtins.find(QStringLiteral("builtin"));
    CHECK(ping != nullptr);
    if (ping)
        CHECK(ping->value == 42);

    // --- 工厂型条目（std::function），证明可承载 PanelFactory 同型条目 ---
    Registry<std::function<int(int)>> factories;
    CHECK(factories.insert("inc", [](int x) { return x + 1; }));
    auto *fn = factories.find("inc");
    CHECK(fn != nullptr);
    if (fn)
        CHECK((*fn)(9) == 10);

    if (g_failed)
    {
        std::printf("registry_standalone: %d FAILED\n", g_failed);
        return 1;
    }
    std::printf("registry_standalone: ALL PASS\n");
    return 0;
}
