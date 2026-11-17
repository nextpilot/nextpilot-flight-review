#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <functional>

class QWidget;
class QLabel;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ps::shell
{

// 属性面板控制 Dock 的唯一入口：返回 false 表示 Dock 不存在。
using DockShow = std::function<bool(const QString &id, bool activate)>;

// 属性面板要展示的内容：标题 + 键值行 + 可编辑键 + 上下文类型。
struct PropertyContent
{
    QString title;
    QVector<QPair<QString, QString>> rows;
    QStringList editable;
    int kind = 0;
};

// 对象树节点；content/apply 为空表示纯分组。
struct PropertyNode
{
    QString id;
    QString parentId;
    QString label;
    QString icon;
    std::function<PropertyContent()> content;
    std::function<void(const QString &key, const QString &text)> apply;
};

// shell 级服务：对象树 + 属性面板两个 dock，各模块只注册节点和读写回调。
class PropertyManager : public QObject
{
    Q_OBJECT

public:
    explicit PropertyManager(QWidget *parent, DockShow showDock);

    void registerNode(const PropertyNode &node);
    void removeNode(const QString &id);
    void clearChildren(const QString &parentId);
    void updateNodeLabel(const QString &id, const QString &label);
    void selectNode(const QString &id);
    void refreshCurrent();
    void showContent(const PropertyContent &content);
    PropertyContent currentContent() const { return m_last; }
    bool hasNode(const QString &id) const { return m_nodes.contains(id); }
    bool hasNodes() const { return !m_nodes.isEmpty(); }

    QWidget *createTreeView();
    QWidget *createPanelView();

private:
    void buildTree();
    void buildPanel();
    void showNode(const QString &id);
    void fillRows(const PropertyContent &content);
    void ensureVisible();
    void pushEdit(const QString &key, const QString &text);

    QWidget *m_parent = nullptr;
    DockShow m_showDock;
    QHash<QString, PropertyNode> m_nodes;
    QStringList m_order;
    QHash<QString, QTreeWidgetItem *> m_items;
    QTreeWidget *m_tree = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_title = nullptr;
    bool m_updating = false;
    bool m_building = false;
    bool m_docksShown = false;
    bool m_treeDirty = false;
    QString m_currentId;
    PropertyContent m_last;
};
} // namespace ps::shell
