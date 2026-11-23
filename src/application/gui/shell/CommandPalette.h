#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QTreeWidget;

namespace nextpilot::plotscope::gui
{

// VS Code 式命令面板：MainWindow 的子控件叠加层。
// 子控件而非独立窗口，便于随主窗口截图，也不需要额外的窗口生命周期。
class CommandPalette : public QWidget
{
public:
    struct Item
    {
        QString id;
        QString title;
        QString category;
        QString shortcut;
        QString reason; // 非空 = 灰显
        QString keywords;
        std::function<void()> action;
    };

    explicit CommandPalette(QWidget *parent = nullptr);

    void setActivationHandler(std::function<void(const QString &)> handler);

    void openCommands(const QList<Item> &items, const QString &title = QStringLiteral("命令面板"));
    void openQuickSwitch(const QList<Item> &items);
    void closePalette();

    QString mode() const { return m_mode; }
    QString query() const;
    void setQuery(const QString &query);
    QStringList visibleIds() const;
    int visibleCount() const;
    bool activateCurrent();

    // 最近使用：Palette 实例跨会话保留，打开时命中项排前。
    QStringList mru() const { return m_mru; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;

private:
    void rebuild();
    bool matches(const Item &item, const QString &query) const;
    void activateItem(const Item &item);

    QLineEdit *m_input = nullptr;
    QLabel *m_titleLabel = nullptr;
    QTreeWidget *m_list = nullptr;
    QList<Item> m_items;
    QString m_mode = QStringLiteral("commands");
    QStringList m_mru;
    std::function<void(const QString &)> m_activationHandler;
};

} // namespace nextpilot::plotscope::gui
