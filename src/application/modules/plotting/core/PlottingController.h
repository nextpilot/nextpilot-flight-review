#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ps::plotting
{

// 页面模型只存标识与状态，不持有 QWidget。
struct PlotPageModel
{
    QString id;
    QString title;
    QJsonObject state;
};

// 绘图模块的非 UI 状态：页面顺序、当前页、页面标题/状态。
// 画布和页签仍由 widget 层按模型创建。
class PlottingController
{
public:
    PlottingController() = default;
    ~PlottingController() = default;

    void clearPages();
    QString addPage(const QString &title);
    QString insertPage(int index, const QString &title);
    bool removePage(const QString &id);
    void movePage(const QString &id, int index);

    int pageCount() const { return m_pages.size(); }
    const QVector<PlotPageModel> &pages() const { return m_pages; }
    QString pageIdAt(int index) const;
    int indexOf(const QString &id) const;

    QString currentPageId() const { return m_currentPageId; }
    void setCurrentPage(const QString &id);

    void setPageState(const QString &id, const QJsonObject &state);
    QJsonObject pageState(const QString &id) const;

private:
    QVector<PlotPageModel> m_pages;
    QString m_currentPageId;
};

} // namespace ps::plotting
