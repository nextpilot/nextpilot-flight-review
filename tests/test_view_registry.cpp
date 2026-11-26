// A8b-M2 ViewRegistry 单测：视图声明登记 + registerViewProvider + unmount 整批撤。
// 只链 ps_registry + Qt6::Core，工厂返回空 QWidget*（不构造真实控件），不碰 Gui。

#include "ViewRegistry.h"
#include "ContributionParser.h"
#include "IActionProvider.h"
#include "IEditorProvider.h"
#include "ResourceRef.h"
#include "RegistryTree.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>

using namespace nextpilot::plotscope::registry;

namespace
{
int g_failed = 0;

// M3-a：假编辑器 provider（不构造控件，仅验证注册/匹配/撤销链路）。
class FakeEditorProvider final : public IEditorProvider
{
public:
    bool canOpen(const ResourceRef &) const override { return true; }
    QWidget *createEditor(const ResourceRef &, QWidget *) override { return nullptr; }
    // 3a-4：readonly 语义——置位后壳据此渲染只读页签、禁保存（headless 验契约面）。
    bool ro = false;
    bool readonly() const override { return ro; }
    int saveCalls = 0;
    bool save(const ResourceRef &) override
    {
        ++saveCalls;
        return !ro; // readonly 提供者自身保存失败（壳本不该调）
    }
};

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)

ContributesDoc doc(const char *json, const QString &owner)
{
    auto obj = QJsonDocument::fromJson(QByteArray(json)).object();
    return parseContributes(obj, owner).doc;
}

// 一个不构造控件的假工厂：返回空指针，仅验证 provider 注册/撤销链路。
ViewRegistry::ViewFactory dummyFactory()
{
    return [](nextpilot::plotscope::context::AppContext &,
              nextpilot::plotscope::intent::IIntentExecutor &, QWidget *) -> QWidget * {
        return nullptr;
    };
}

// 假动作提供器：模拟 MRU/收藏动态 gallery（裁定 §2）。
// 返回纯数据条目；内容变化经 onDidChangeActions 通知外壳只重建该 slot。
class FakeActionProvider final : public QObject, public IActionProvider
{
    Q_OBJECT
public:
    QList<UiAction> actionsFor(const QString &slotId) const override
    {
        QList<UiAction> out;
        for (const QString &group : m_groups)
        {
            UiAction a;
            a.id = slotId + QStringLiteral(".") + group;
            a.galleryGroup = group;
            a.order = out.size();
            out.append(a);
        }
        return out;
    }
    QMetaObject::Connection
    onDidChangeActions(std::function<void(const QString &slotId)> listener) override
    {
        return QObject::connect(this, &FakeActionProvider::actionsChanged, this,
                                [listener = std::move(listener)](const QString &slot) {
                                    listener(slot);
                                });
    }
    // 模拟"最近使用/收藏"条目变化。
    void setGroups(const QStringList &g)
    {
        m_groups = g;
        Q_EMIT actionsChanged(QStringLiteral("ribbon.group.analysis.apps"));
    }
    QStringList m_groups{QStringLiteral("最近使用"), QStringLiteral("收藏")};
signals:
    void actionsChanged(const QString &slotId);
};
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ViewRegistry views;

    // 声明层：两个 owner 各一份 contributes。
    views.declare(doc(R"({
        "viewsContainers":[
            {"id":"plot.side","title":"绘图","area":"sidebar","order":30},
            {"id":"plot.bottom","title":"日志","area":"bottom"}
        ],
        "views":[
            {"id":"plot.props","name":"属性","container":"plot.side","order":5},
            {"id":"plot.canvas","name":"画布","container":"plot.side","order":1}
        ]
    })", QStringLiteral("plot")));
    views.declare(doc(R"({
        "viewsContainers":[{"id":"data.side","title":"数据","area":"sidebar"}],
        "views":[{"id":"data.props","name":"数据源","container":"data.side"}]
    })", QStringLiteral("data")));

    CHECK(views.declaredOwners().size() == 2);
    CHECK(views.containersOf(QStringLiteral("plot")).size() == 2);
    CHECK(views.viewsOf(QStringLiteral("plot")).size() == 2);
    CHECK(views.viewsIn(QStringLiteral("plot.side")).size() == 2);
    // viewsIn 按 order：canvas(1) 在 props(5) 前。
    CHECK(views.viewsIn(QStringLiteral("plot.side")).at(0).id == QLatin1String("plot.canvas"));
    CHECK(views.viewsIn(QStringLiteral("plot.side")).at(1).id == QLatin1String("plot.props"));

    // 运行层：registerViewProvider，先到者为准。
    CHECK(views.registerViewProvider(QStringLiteral("plot.props"), dummyFactory(), QStringLiteral("plot")));
    CHECK(!views.registerViewProvider(QStringLiteral("plot.props"), dummyFactory(), QStringLiteral("other")));
    CHECK(views.provider(QStringLiteral("plot.props")) != nullptr);
    CHECK(views.provider(QStringLiteral("plot.props"))->owner == QLatin1String("plot"));
    CHECK(views.provider(QStringLiteral("missing")) == nullptr);

    // 卸载 owner：声明 + provider 整批撤，另一 owner 不受影响。
    CHECK(views.unregisterOwner(QStringLiteral("plot")) == 1);
    views.undeclare(QStringLiteral("plot"));
    CHECK(views.provider(QStringLiteral("plot.props")) == nullptr);
    CHECK(views.containersOf(QStringLiteral("plot")).isEmpty());
    CHECK(views.viewsOf(QStringLiteral("plot")).isEmpty());
    CHECK(views.declaredOwners().size() == 1);
    CHECK(views.viewsOf(QStringLiteral("data")).size() == 1); // data 不受影响

    // 动作提供器（裁定 52441074 §2）：同层注册 + owner 校验 + 整批撤。
    FakeActionProvider ap1;
    FakeActionProvider ap2;
    CHECK(views.registerActionProvider(QStringLiteral("plot"), QStringLiteral("ribbon.group.plot.series"), &ap1));
    CHECK(!views.registerActionProvider(QStringLiteral("plot"), QStringLiteral("ribbon.group.plot.series"), &ap2)); // 同 owner 同 slot 拒
    CHECK(views.registerActionProvider(QStringLiteral("data"), QStringLiteral("ribbon.group.plot.series"), &ap2)); // 不同 owner 可
    CHECK(views.actionProvidersFor(QStringLiteral("ribbon.group.plot.series")).size() == 2);
    CHECK(views.actionProvidersFor(QStringLiteral("no.such.slot")).isEmpty());
    CHECK(views.actionProviders().size() == 2);

    // MRU/收藏动态 gallery 活着（裁定 §2）：provider 初始返回"最近使用/收藏"两组，
    // 内容变化经 onDidChangeActions 通知，actionsFor 立即反映新条目。
    int notified = 0;
    QString notifiedSlot;
    ap1.onDidChangeActions([&](const QString &slot) { ++notified; notifiedSlot = slot; });
    CHECK(ap1.actionsFor(QStringLiteral("ribbon.group.analysis.apps")).size() == 2);
    ap1.setGroups({QStringLiteral("最近使用"), QStringLiteral("收藏"), QStringLiteral("固定")});
    CHECK(notified == 1);
    CHECK(notifiedSlot == QLatin1String("ribbon.group.analysis.apps"));
    CHECK(ap1.actionsFor(QStringLiteral("ribbon.group.analysis.apps")).size() == 3);

    // 卸载 plot：声明 + view provider + action provider 全撤，data 的 provider 留着。
    views.undeclare(QStringLiteral("plot"));
    CHECK(views.actionProviders().size() == 1);
    CHECK(views.actionProvidersFor(QStringLiteral("ribbon.group.plot.series")).at(0).owner == QLatin1String("data"));

    views.undeclare(QStringLiteral("data"));
    CHECK(views.declaredOwners().isEmpty());
    CHECK(views.viewsIn(QStringLiteral("data.side")).isEmpty());
    CHECK(views.actionProviders().isEmpty());

    // ===== M3-a 第 2 笔：registerEditorProvider 防冒名 + dispose + selector 匹配 + 整批撤 =====
    {
        ViewRegistry eds;
        FakeEditorProvider web;
        FakeEditorProvider text;
        eds.declare(doc(R"({"customEditors":[
            {"viewType":"web","displayName":"网页","priority":5,"selector":[{"mime":"text/html"}]},
            {"viewType":"text","displayName":"文本","priority":0,"selector":[{"mime":"text/plain"},{"filenamePattern":"*.psx"}]}
        ]})", QStringLiteral("ext.a")));
        // 防冒名：owner ≠ 声明者 → 拒；未声明 viewType → 拒；空 owner/viewType → 拒。
        CHECK(!eds.registerEditorProvider(QStringLiteral("web"), &web, QStringLiteral("ext.b")));
        CHECK(!eds.registerEditorProvider(QStringLiteral("nope"), &web, QStringLiteral("ext.a")));
        CHECK(!eds.registerEditorProvider(QStringLiteral("web"), &web, QString()));
        // 声明者注册 → 成功（Disposable 句柄）。
        auto d1 = eds.registerEditorProvider(QStringLiteral("web"), &web, QStringLiteral("ext.a"));
        auto d2 = eds.registerEditorProvider(QStringLiteral("text"), &text, QStringLiteral("ext.a"));
        CHECK(d1 != nullptr && d2 != nullptr);
        // 重复注册同一 viewType → 先到者为准，后到者拒。
        CHECK(!eds.registerEditorProvider(QStringLiteral("web"), &text, QStringLiteral("ext.a")));
        // selector 匹配：mime 精确；filenamePattern glob（*.psx）大小写不敏感。
        ResourceRef html{QStringLiteral("https://e.com"), QStringLiteral("text/html"), QString()};
        ResourceRef docx{QStringLiteral("a.PSX"), QString(), QString()}; // pattern 命中 text
        ResourceRef bin{QStringLiteral("a.bin"), QStringLiteral("application/octet"), QString()};
        CHECK(eds.editorViewTypeFor(html) == QLatin1String("web"));
        CHECK(eds.editorViewTypeFor(docx) == QLatin1String("text"));
        CHECK(eds.editorViewTypeFor(bin).isEmpty());
        // 回退反证（3a-4）：无匹配 customEditor 的资源不得误回退到已注册编辑器——
        // application/octet 资源即使 provider canOpen 全放行，也因 selector 不中而空匹配；
        // 回退到内置/默认编辑器是壳的打开策略，声明层只回答「有没有 customEditor 命中」。
        CHECK(eds.editorProvider(QStringLiteral("web")) == &web); // provider 在位也不误配
        ResourceRef oct{QStringLiteral("a.bin"), QStringLiteral("application/octet"), QString()};
        CHECK(eds.editorViewTypeFor(oct).isEmpty());
        // readonly 语义（3a-4）：readonly 提供者 readonly() 传播契约面；壳据此禁保存。
        FakeEditorProvider roProv;
        roProv.ro = true;
        CHECK(roProv.readonly());
        CHECK(!roProv.save(ResourceRef{})); // readonly 提供者自身保存失败
        CHECK(roProv.saveCalls == 1);
        CHECK(eds.editorProvider(QStringLiteral("web")) == &web);
        // dispose 撤销该条注册；provider 借用指针不悬空（表项直接移除）。
        d1->dispose();
        d1->dispose(); // 幂等
        CHECK(eds.editorProvider(QStringLiteral("web")) == nullptr);
        CHECK(eds.editorProvider(QStringLiteral("text")) == &text);
        // owner 卸载整批撤：声明 + provider 全消失。
        eds.undeclare(QStringLiteral("ext.a"));
        CHECK(eds.allCustomEditors().isEmpty());
        CHECK(eds.editorProvider(QStringLiteral("text")) == nullptr);
        CHECK(eds.editorOwner(QStringLiteral("text")).isEmpty());
        CHECK(eds.editorViewTypeFor(html).isEmpty());
    }

    // ===== A2b 笔 2a：views[].icon 解析（对位 ViewDescriptor.iconPath，纯名字）=====
    {
        ViewRegistry vreg;
        vreg.declare(doc(R"({"viewsContainers":[{"id":"explorer","title":"资源管理器","area":"sidebar"}],
            "views":[{"id":"data.tree","name":"数据树","container":"explorer","icon":"database"},
                     {"id":"plot.axes","name":"坐标轴","container":"explorer"}]})",
                         QStringLiteral("ext.view")));

        // 声明的 icon 原样解析进 ContributedView；未声明者为空（壳不猜、不兜底）。
        QString treeIcon;
        int axesCount = 0;
        for (const auto &v : vreg.allViews())
        {
            if (v.id == QLatin1String("data.tree"))
                treeIcon = v.icon;
            if (v.id == QLatin1String("plot.axes") && v.icon.isEmpty())
                ++axesCount;
        }
        CHECK(treeIcon == QLatin1String("database"));
        CHECK(axesCount == 1);
    }

    if (g_failed == 0)
        std::printf("view_registry: ALL PASS\n");
    else
        std::printf("view_registry: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}

#include "test_view_registry.moc"
