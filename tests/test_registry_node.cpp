// M2.8 Registry 机制整改（R1）验收：
//   2.8.2 RegistryNode/RegistryTree 机制——上树/摘树/kind 查找/树枚举/信号到达/测试间树隔离
//   2.8.3 diag.registries 意图——能枚举已上树节点；空树返回空列表
//
// 前半段只依赖 ps_registry（机制本体）；diag 段走真实执行路径：
// App 装配 + interactions().dispatchViewAction（与 GUI/脚本同一条路）。

#include "RegistryNode.h"
#include "RegistryTree.h"

#include "App.h"
#include "Dataset.h"
#include "DatasetHub.h"
#include "InteractionHub.h"

#include <QCoreApplication>
#include <QHash>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>

using nextpilot::plotscope::dataset::Dataset;
using nextpilot::plotscope::registry::kindName;
using nextpilot::plotscope::registry::RegistryKind;
using nextpilot::plotscope::registry::RegistryNode;
using nextpilot::plotscope::registry::RegistryTree;

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace
{

// 测试用最小节点：count/keys 固定，可手动发射信号（R1 尚无 TypedRegistry）。
class MockNode : public RegistryNode
{
public:
    MockNode(RegistryTree &tree, RegistryKind kind, const QString &name) : RegistryNode(tree, kind, name) {}

    int count() const override { return 2; }
    QStringList keysAsStrings() const override { return {QStringLiteral("a"), QStringLiteral("b")}; }

    // 供信号到达测试手动发射（TypedRegistry 落地后由增删自动发射）。
    void fireAdded(const QString &key) { Q_EMIT objectAdded(QVariant(key)); }
    void fireRemoved(const QString &key) { Q_EMIT objectRemoved(QVariant(key)); }
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // --- 2.8.2a 上树/树枚举/kind 查找 ---
    RegistryTree tree;
    CHECK(tree.nodes().isEmpty());
    CHECK(tree.find(RegistryKind::Dataset) == nullptr);

    MockNode dataset(tree, RegistryKind::Dataset, QStringLiteral("数据集"));
    MockNode extension(tree, RegistryKind::Extension, QStringLiteral("扩展包"));
    CHECK(tree.nodes().size() == 2);
    CHECK(tree.find(RegistryKind::Dataset) == &dataset);
    CHECK(tree.find(RegistryKind::Extension) == &extension);
    CHECK(tree.find(RegistryKind::UiSlot) == nullptr); // 未上树的 kind 找不到
    CHECK(dataset.kind() == RegistryKind::Dataset);
    CHECK(dataset.name() == QStringLiteral("数据集"));
    CHECK(dataset.count() == 2);
    CHECK(dataset.keysAsStrings().size() == 2);

    // --- kindName：diag JSON 用，7 个 kind 全覆盖 ---
    CHECK(kindName(RegistryKind::Dataset) == QStringLiteral("dataset"));
    CHECK(kindName(RegistryKind::Source) == QStringLiteral("source"));
    CHECK(kindName(RegistryKind::DerivedSeries) == QStringLiteral("derived-series"));
    CHECK(kindName(RegistryKind::Interaction) == QStringLiteral("interaction"));
    CHECK(kindName(RegistryKind::UiSlot) == QStringLiteral("ui-slot"));
    CHECK(kindName(RegistryKind::Extension) == QStringLiteral("extension"));
    CHECK(kindName(RegistryKind::BuiltinModule) == QStringLiteral("builtin-module"));

    // --- 2.8.2b 摘树：析构自动摘除 ---
    {
        MockNode scoped(tree, RegistryKind::UiSlot, QStringLiteral("临时"));
        CHECK(tree.nodes().size() == 3);
        CHECK(tree.find(RegistryKind::UiSlot) == &scoped);
    } // scoped 出析构 → 自动摘除
    CHECK(tree.nodes().size() == 2);
    CHECK(tree.find(RegistryKind::UiSlot) == nullptr);

    // --- 2.8.2c 信号到达（带 key）---
    int addedSeen = 0, removedSeen = 0;
    QString lastKey;
    QObject::connect(&dataset, &RegistryNode::objectAdded,
                     [&](QVariant key) { ++addedSeen; lastKey = key.toString(); });
    QObject::connect(&dataset, &RegistryNode::objectRemoved,
                     [&](QVariant key) { ++removedSeen; lastKey = key.toString(); });
    dataset.fireAdded(QStringLiteral("ds1"));
    dataset.fireRemoved(QStringLiteral("ds2"));
    CHECK(addedSeen == 1);
    CHECK(removedSeen == 1);
    CHECK(lastKey == QStringLiteral("ds2"));

    // --- 2.8.2d 测试间树隔离：两棵树互不可见 ---
    RegistryTree treeB;
    MockNode other(treeB, RegistryKind::BuiltinModule, QStringLiteral("另一棵树"));
    CHECK(treeB.nodes().size() == 1);
    CHECK(treeB.find(RegistryKind::BuiltinModule) == &other);
    CHECK(treeB.find(RegistryKind::Dataset) == nullptr);
    CHECK(tree.nodes().size() == 2); // 树 A 不受影响

    // --- 2.8.3 diag.registries（真实执行路径：App 装配 + dispatchViewAction）---
    nextpilot::plotscope::app::App application;
    // 空树 → 空列表（独立树，不经 App）。
    {
        RegistryTree bare;
        nextpilot::plotscope::registry::TypedRegistry<int> lonely(bare, RegistryKind::Interaction,
                                                                  QStringLiteral("孤立表"));
        // 上树但无条目：count=0、keys 空（空表不是空树，节点仍应枚举到）。
        CHECK(bare.nodes().size() == 1);
        CHECK(lonely.count() == 0);
        CHECK(lonely.keysAsStrings().isEmpty());
    }
    nextpilot::plotscope::foundation::Result<QVariant> out{QVariant{}};
    // M2.8 遗留5 收口：App 组合根树上全量上树——
    // dataset / extension / source / interaction / derived-series / ui-slot
    // （AppContext+RuntimeHost 成员构造序）+ builtin-module
    // / parser / recording-format / recording-backend
    // （host 装配期把四个 static 节点显式挂树，遗留4 收口；A2d：panel-factory
    // 节点随旧面板工厂表退场）。不存在不上树形态。
    CHECK(application.context().interactions().dispatchViewAction(QStringLiteral("diag.registries"), {}, out));
    CHECK(out.ok());
    const QVariantList base = out.value().toList();
    // 10 类节点全量上树、kind 互异（A8b contributes 是 Extension 的属性面，不新增 kind）。
    CHECK(base.size() == 10);
    QHash<QString, QVariantMap> byKind;
    for (const QVariant &r : base)
        byKind.insert(r.toMap().value(QStringLiteral("kind")).toString(), r.toMap());
    CHECK(byKind.size() == 10); // kind 互异
    const QVariantMap dsRow = byKind.value(QStringLiteral("dataset"));
    CHECK(dsRow.value(QStringLiteral("name")).toString() == QStringLiteral("数据集"));
    CHECK(dsRow.value(QStringLiteral("count")).toInt() == 0);
    CHECK(dsRow.value(QStringLiteral("keys")).toStringList().isEmpty());
    // 扩展包节点（App 装配时内置包已 mount，count>=1）。
    const QVariantMap extRow = byKind.value(QStringLiteral("extension"));
    CHECK(extRow.value(QStringLiteral("name")).toString() == QStringLiteral("扩展包"));
    CHECK(extRow.value(QStringLiteral("count")).toInt() >= 1);
    CHECK(extRow.value(QStringLiteral("keys")).toStringList().size() >= 1);
    // 推迟池收口后的 4 类新节点：source / interaction / derived-series / ui-slot。
    CHECK(byKind.value(QStringLiteral("source")).value(QStringLiteral("name")).toString()
          == QStringLiteral("数据源"));
    CHECK(byKind.value(QStringLiteral("source")).value(QStringLiteral("count")).toInt() == 0);
    CHECK(byKind.value(QStringLiteral("interaction")).value(QStringLiteral("count")).toInt() >= 1);
    // diag.registries 本身就是一条 viewAction，interaction 节点至少含它。
    CHECK(byKind.value(QStringLiteral("interaction"))
              .value(QStringLiteral("keys"))
              .toStringList()
              .contains(QStringLiteral("diag.registries")));
    CHECK(byKind.value(QStringLiteral("derived-series")).value(QStringLiteral("name")).toString()
          == QStringLiteral("派生序列"));
    CHECK(byKind.value(QStringLiteral("ui-slot")).value(QStringLiteral("count")).toInt() >= 1);
    CHECK(byKind.value(QStringLiteral("ui-slot"))
              .value(QStringLiteral("keys"))
              .toStringList()
              .contains(QStringLiteral("ribbon.page.file")));
    // static 节点（headless 无 GUI 视图注册 → 内置模块>=1；A2d：panel-factory
    // 节点随工厂表退场，不再有独立行）。
    const QVariantMap moduleRow = byKind.value(QStringLiteral("builtin-module"));
    CHECK(moduleRow.value(QStringLiteral("name")).toString() == QStringLiteral("内置模块"));
    CHECK(moduleRow.value(QStringLiteral("count")).toInt() >= 1);
    CHECK(moduleRow.value(QStringLiteral("keys")).toStringList().contains(QStringLiteral("mod.data")));
    // 遗留 4 收口的三张存量静态表节点（headless：表内容视装配而定，只验上树与视图口径）。
    CHECK(byKind.value(QStringLiteral("parser")).value(QStringLiteral("name")).toString()
          == QStringLiteral("解析器"));
    CHECK(byKind.value(QStringLiteral("recording-format")).value(QStringLiteral("name")).toString()
          == QStringLiteral("录制格式"));
    CHECK(byKind.value(QStringLiteral("recording-backend")).value(QStringLiteral("name")).toString()
          == QStringLiteral("录制后端"));

    // 加两个数据集再调 → count/keys 同步（keys 为 id 字符串）。
    auto *repo = &application.context().data();
    const int id1 = repo->add(Dataset::create(QStringLiteral("ds-a"), QStringLiteral("mem")));
    const int id2 = repo->add(Dataset::create(QStringLiteral("ds-b"), QStringLiteral("mem")));
    CHECK(id1 >= 0 && id2 > id1);
    CHECK(application.context().interactions().dispatchViewAction(QStringLiteral("diag.registries"), {}, out));
    const QVariantList rows = out.value().toList();
    CHECK(rows.size() == 10); // 10 类节点全量上树，数量不随数据集增删变化
    const QVariantMap row = rows.first().toMap();
    CHECK(row.value(QStringLiteral("kind")).toString() == QStringLiteral("dataset"));
    CHECK(row.value(QStringLiteral("count")).toInt() == 2);
    const QStringList dsKeys = row.value(QStringLiteral("keys")).toStringList();
    CHECK(dsKeys.size() == 2);
    CHECK(dsKeys.contains(QString::number(id1)));
    CHECK(dsKeys.contains(QString::number(id2)));

    // TypedRegistry 增删语义（Dataset 薄壳上收的通用行为）：addWithStableId 先到者为准。
    auto dup = repo->addWithStableId(Dataset::create(QStringLiteral("dup"), QStringLiteral("mem")), id1);
    CHECK(!dup.ok());
    auto stable = repo->addWithStableId(Dataset::create(QStringLiteral("stable"), QStringLiteral("mem")), 42);
    CHECK(stable.ok() && stable.value() == 42);
    CHECK(repo->add(Dataset::create(QStringLiteral("auto"), QStringLiteral("mem"))) == 43); // 游标推进
    CHECK(repo->remove(42));
    CHECK(repo->get(42) == nullptr);

    std::printf("OK ps_test_registry_node\n");
    return 0;
}
