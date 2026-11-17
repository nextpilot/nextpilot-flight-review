#pragma once

#include "PlottingController.h"

namespace ps::plotting
{

// 绘图只读服务：供属性面板、脚本桥等只读消费页面模型。
class PlottingService
{
public:
    explicit PlottingService(const PlottingController &controller) : m_controller(&controller) {}

    int pageCount() const { return m_controller->pageCount(); }
    const QVector<PlotPageModel> &pages() const { return m_controller->pages(); }
    QString currentPageId() const { return m_controller->currentPageId(); }
    QString pageIdAt(int index) const { return m_controller->pageIdAt(index); }
    int indexOf(const QString &id) const { return m_controller->indexOf(id); }
    QJsonObject pageState(const QString &id) const { return m_controller->pageState(id); }

private:
    const PlottingController *m_controller = nullptr;
};

} // namespace ps::plotting
