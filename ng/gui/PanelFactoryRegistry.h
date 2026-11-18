#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace ng::gui
{

// 部件工厂表：把"部件类型标识"映射到具体的 QWidget 构造过程。
//
// 这是 app 骨架与器官之间的解耦点：
//   器官只说"我需要一个 dataset-tree 面板"（requestPanel("dock.left", "dataset-tree")），
//   骨架决定用哪个实现（DataManagerPanel）。器官因此不需要依赖 Qt Widgets，
//   headless（CLI）构建保持纯净；新增器官自带界面也不用改 GUI 层。
class PanelFactoryRegistry
{
public:
    using Factory = std::function<QWidget *(QWidget *parent)>;

    // 注册一种部件类型。重复注册以先到者为准。
    static void registerPanel(const QString &type, Factory factory);

    // 按类型创建部件；未注册返回 nullptr。
    static QWidget *create(const QString &type, QWidget *parent);

    // 已注册的部件类型（诊断/测试用）。
    static QStringList types();
};

} // namespace ng::gui
