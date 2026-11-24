#pragma once

#include <QVector>
#include <QWidget>

class QEvent;
class QLabel;

namespace ads
{
class CDockWidget;
} // namespace ads

namespace nextpilot::plotscope::gui
{

// 每格顶部的标题条：格名、拆分/最大化/书签/A-B/关闭按钮，兼作拖动把手。
// 拖动与落位由 ADS 的原生标题栏执行，本条只把条上的鼠标事件转给它。
class AxesToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit AxesToolbar(QWidget *parent = nullptr);

    // 本条属于哪个停靠面板。面板在停靠树重建时才创建，故装配后注入。
    void setDockWidget(ads::CDockWidget *dockWidget);
    ads::CDockWidget *dockWidget() const { return m_dockWidget; }

    void setTitle(const QString &title);
    QString title() const;

    // 亮/暗：暗只把底色、格名、按钮藏起来，仍占位，进出格时布局不跳。
    void setShown(bool shown);
    bool isShown() const { return m_shown; }

    // 同一停靠区里有多格（页签）时由外部置位：本条让位给页签条。
    void setSuppressed(bool suppressed);
    bool isSuppressed() const { return m_suppressed; }

Q_SIGNALS:
    void activated();
    void splitClicked(bool horizontal);
    void maximizeClicked();
    void bookmarkClicked();
    void abClicked();
    void closeClicked();
    void titleEdited(const QString &title);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void forwardToTitleBar(QEvent *event, bool syntheticPress);
    void rename();

    ads::CDockWidget *m_dockWidget = nullptr;
    QLabel *m_title = nullptr;
    QVector<QWidget *> m_buttons;
    bool m_shown = true;
    bool m_suppressed = false;
};

} // namespace nextpilot::plotscope::gui
