#pragma once

#include <QString>
#include <QTabWidget>

namespace nextpilot::plotscope::gui
{

// 绘图页的页签栏：末尾常驻一个「＋」，页索引永远落在 0..pageCount()-1。
// 上层只按真实页索引说话，不再自己减「＋」哨兵。
class PageTabs : public QTabWidget
{
    Q_OBJECT
public:
    explicit PageTabs(QWidget *parent = nullptr);

    int insertPage(QWidget *host, const QString &title); // 永远插在「＋」之前
    void removePageAt(int index);
    int pageCount() const;                                // 不含「＋」
    int currentPage() const;
    void setCurrentPage(int index);
    void setPageTitle(int index, const QString &title);
    QString pageTitle(int index) const { return tabText(index); }

Q_SIGNALS:
    void pageSelected(int index); // 只对真实页发
    void pageCloseRequested(int index);
    void titleEdited(int index, const QString &title);
    void addPageRequested(); // 点了末尾「＋」

private:
    int plusIndex() const;
};

} // namespace nextpilot::plotscope::gui
