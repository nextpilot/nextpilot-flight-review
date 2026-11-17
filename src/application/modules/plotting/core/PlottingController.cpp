#include "PlottingController.h"

#include <QUuid>

namespace ps::plotting
{

void PlottingController::clearPages()
{
    m_pages.clear();
    m_currentPageId.clear();
}

QString PlottingController::addPage(const QString &title)
{
    return insertPage(m_pages.size(), title);
}

QString PlottingController::insertPage(int index, const QString &title)
{
    PlotPageModel page;
    page.id = QStringLiteral("page-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    page.title = title;
    const int at = qBound(0, index, m_pages.size());
    m_pages.insert(at, page);
    if (m_currentPageId.isEmpty())
        m_currentPageId = page.id;
    return page.id;
}

bool PlottingController::removePage(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    m_pages.removeAt(index);
    if (m_currentPageId == id)
        m_currentPageId = m_pages.isEmpty() ? QString() : m_pages.value(qMin(index, m_pages.size() - 1)).id;
    return true;
}

void PlottingController::movePage(const QString &id, int index)
{
    const int from = indexOf(id);
    if (from < 0)
        return;
    PlotPageModel page = m_pages.takeAt(from);
    m_pages.insert(qBound(0, index, m_pages.size()), page);
}

QString PlottingController::pageIdAt(int index) const
{
    return index >= 0 && index < m_pages.size() ? m_pages.at(index).id : QString();
}

int PlottingController::indexOf(const QString &id) const
{
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages.at(i).id == id)
            return i;
    return -1;
}

void PlottingController::setCurrentPage(const QString &id)
{
    if (indexOf(id) >= 0)
        m_currentPageId = id;
}

void PlottingController::setPageState(const QString &id, const QJsonObject &state)
{
    const int index = indexOf(id);
    if (index >= 0)
        m_pages[index].state = state;
}

QJsonObject PlottingController::pageState(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? m_pages.at(index).state : QJsonObject();
}

} // namespace ps::plotting
