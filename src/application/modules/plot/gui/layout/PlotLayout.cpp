#include "PlotLayout.h"

#include "DockAreaWidget.h"
#include "DockSplitter.h"
#include "DockedAxes.h"

#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace nextpilot::plotscope::gui
{

PlotLayout::PlotLayout(QWidget *host) : ads::CDockManager(host)
{
    setObjectName(QStringLiteral("plotPlotLayout"));
    // 画布区的标题栏与样式由主题统一管，先关掉 ADS 自带样式表免得打架。
    setStyleSheet(QString());

    // 新出现的停靠区一律先藏单格标题栏：它是页签条，只在合并成多格时才该出现。
    QObject::connect(this, &ads::CDockManager::dockAreaCreated, this,
                     [](ads::CDockAreaWidget *area) {
                         if (area)
                             area->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
                     });

    if (auto *lay = qobject_cast<QVBoxLayout *>(host->layout()))
        lay->addWidget(this);
    else
    {
        auto *hostLay = new QVBoxLayout(host);
        hostLay->setContentsMargins(0, 0, 0, 0);
        hostLay->setSpacing(0);
        hostLay->addWidget(this);
    }

    // 面板增删统一转成"格面板"信号，上层不必再认识 ADS 的 dock widget 类型。
    QObject::connect(this, &ads::CDockManager::dockWidgetAdded, this, [this](ads::CDockWidget *dock) {
        if (auto *cellDock = qobject_cast<DockedAxes *>(dock))
            Q_EMIT cellDockAdded(cellDock);
    });
    QObject::connect(this, &ads::CDockManager::dockWidgetRemoved, this, [this](ads::CDockWidget *dock) {
        if (auto *cellDock = qobject_cast<DockedAxes *>(dock))
            Q_EMIT cellDockRemoved(cellDock);
    });
}

DockedAxes *PlotLayout::addCellDock(const CellSlot &slot, int side, DockedAxes *relative,
                                   bool intoContainer)
{
    if (!slot.frame)
        return nullptr;

    auto *dock = new DockedAxes(this, slot.title, slot.frame, slot.bar);
    const ads::DockWidgetArea area = (side == 0)   ? ads::CenterDockWidgetArea
                                    : (side == 1) ? ads::RightDockWidgetArea
                                                  : ads::BottomDockWidgetArea;

    ads::CDockAreaWidget *areaWidget = nullptr;
    if (intoContainer)
        areaWidget = addDockWidgetToContainer(area, dock, this); // 整宽换行
    else if (relative)
        areaWidget = addDockWidget(area, dock, relative->dockAreaWidget());
    else
        areaWidget = addDockWidget(area, dock);

    if (areaWidget)
    {
        areaWidget->setAllowedAreas(ads::OuterDockAreas);
        areaWidget->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
    }
    slot.frame->show();
    return dock;
}

QVector<DockedAxes *> PlotLayout::layoutGrid(const QVector<CellSlot> &cellSlots, int rows, int cols)
{
    QVector<DockedAxes *> docks;
    docks.reserve(cellSlots.size());
    if (cellSlots.isEmpty())
        return docks;

    rows = std::max(1, rows);
    cols = std::max(1, cols);
    int index = 0;
    for (int r = 0; r < rows; ++r)
    {
        DockedAxes *prevInRow = nullptr;
        for (int c = 0; c < cols && index < cellSlots.size(); ++c, ++index)
        {
            DockedAxes *dock = nullptr;
            if (r == 0 && c == 0)
                dock = addCellDock(cellSlots[index], 0);                // 首格占住整页
            else if (r == 0)
                dock = addCellDock(cellSlots[index], 1, prevInRow);      // 首行往右排
            else if (c == 0)
                dock = addCellDock(cellSlots[index], 2, nullptr, true);  // 换行整宽插到容器下方
            else
                dock = addCellDock(cellSlots[index], 1, prevInRow);      // 行内往右
            if (!dock)
                return docks;
            docks.append(dock);
            prevInRow = dock;
        }
    }
    return docks;
}

void PlotLayout::equalizeSplitters()
{
    // 严格等分：分隔器按相对权重分配空间，所有项给同一权重即等宽/等高
    // （拖分隔条时权重被改写，点 m×n 后由这里重新抹平）。
    auto apply = [this]() {
        const auto splitters = findChildren<ads::CDockSplitter *>();
        for (auto *splitter : splitters)
        {
            const int count = splitter->count();
            if (count <= 0)
                continue;
            QList<int> sizes;
            sizes.reserve(count);
            for (int i = 0; i < count; ++i)
                sizes.append(1000000 / count);
            splitter->setSizes(sizes);
        }
    };
    // 首次布局时控件尺寸还是 0，等事件循环走完再抹平一次。
    apply();
    QTimer::singleShot(0, this, apply);
}

} // namespace nextpilot::plotscope::gui
