#include "MainWindow.h"

#include "DataManagerPanel.h"
#include "DockManager.h"
#include "GuiContext.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "RibbonBar.h"

#include "Dataset.h"
#include "IModule.h"
#include "ModuleHost.h"
#include "UiSlotRegistry.h"

#include <QMenuBar>
#include <QStatusBar>
using namespace ng::core;
using namespace ng::host;

namespace ng::gui
{

MainWindow::MainWindow(DataRepository &repo, QWidget *parent)
    : QMainWindow(parent), m_repo(repo)
{
    setWindowTitle(QStringLiteral("PlotScope NG"));
    resize(1280, 800);

    // 骨架：三个容器，各自由独立文件里的独立类实现。
    m_ribbon = new RibbonBar(this);
    m_docks = new DockManager(this);
    m_canvas = new PlotCanvas(this);

    // 菜单栏（薄壳：真正的动作由器官/工具箱声明，这里只放容器）
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->setObjectName(QStringLiteral("menu.file"));

    statusBar()->showMessage(QStringLiteral("就绪"));

    // 部件工厂：器官只描述"我需要什么部件"，由骨架决定用哪个实现。
    // 这样模块不依赖 Qt Widgets（CLI 保持纯净），新增器官自带界面也无需改 GUI 层。
    //
    // 注意：数据集树面板由 mod.data 器官通过 requestPanel 申请（见 DataModule::onAttach），
    // 骨架只提供工厂实现——面板归器官所有，骨架不认识它。
    PanelFactoryRegistry::registerPanel(
        QStringLiteral("dataset-tree"),
        [this](QWidget *parent) -> QWidget * {
            auto *panel = new DataManagerPanel(m_repo, parent);
            connect(panel, &DataManagerPanel::channelActivated, this,
                    [this](int dsId, const QString &table, const QString &column) {
                        showChannel(dsId, table, column);
                    });
            return panel;
        });
    PanelFactoryRegistry::registerPanel(
        QStringLiteral("plot-canvas"), [this](QWidget *parent) -> QWidget * {
            return new PlotCanvas(parent);
        });
}

MainWindow::~MainWindow() = default;

void MainWindow::declareStandardSlots()
{
    // app 骨架提供的标准插槽。工具箱与器官往这些槽里填内容，
    // 骨架代码此后不再变动。
    UiSlotRegistry &slotReg = m_context->slotRegistry();
    slotReg.declareSlot(QStringLiteral("ribbon.page.home"), QStringLiteral("主页"),
                      QStringLiteral("app.skeleton"), SlotKind::RibbonPage);
    slotReg.declareSlot(QStringLiteral("ribbon.group.home.data"), QStringLiteral("数据"),
                      QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                      QStringLiteral("ribbon.page.home"));
    slotReg.declareSlot(QStringLiteral("ribbon.group.home.view"), QStringLiteral("视图"),
                      QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                      QStringLiteral("ribbon.page.home"));
    slotReg.declareSlot(QStringLiteral("dock.left"), QStringLiteral("数据集"),
                      QStringLiteral("app.skeleton"), SlotKind::DockPanel);
    slotReg.declareSlot(QStringLiteral("dock.right"), QStringLiteral("属性"),
                      QStringLiteral("app.skeleton"), SlotKind::DockPanel);
    slotReg.declareSlot(QStringLiteral("canvas.center"), QStringLiteral("画布"),
                      QStringLiteral("app.skeleton"), SlotKind::Canvas);
    slotReg.declareSlot(QStringLiteral("menu.plot"), QStringLiteral("绘图"),
                      QStringLiteral("app.skeleton"), SlotKind::Menu);
}

void MainWindow::build(ModuleHost &host)
{
    m_host = &host;
    UiSlotRegistry &slotReg = host.uiSlotsMutable();
    m_context = new GuiContext(this, slotReg, m_ribbon, m_docks, m_canvas, &host, this);

    // 1) 骨架插槽
    declareStandardSlots();

    // 2) 数据面板停靠到左侧
    m_context->placePanel(m_dataPanel, QStringLiteral("dock.left"));

    // 3) 画布进中央
    m_context->placeCanvas(m_canvas);

    // 4) 装器官（GUI 模式：会调用 onAttach，器官可自带界面入口）
    host.activate(QStringList(), m_context);

    // 5) ribbon 重建：把工具箱的 UI 贡献 + 器官的本地入口一起渲染出来
    m_context->refreshRibbon();

    // 6) 记录器官申请的部件（便于诊断与测试）
    if (auto *panel = qobject_cast<DataManagerPanel *>(m_docks->widget(QStringLiteral("dock.left"))))
        m_dataPanel = panel;
}

void MainWindow::showChannel(int datasetId, const QString &table, const QString &column)
{
    const Dataset *ds = m_repo.get(datasetId);
    if (!ds)
        return;
    const Table *t = ds->table(table);
    if (!t || !t->column(column))
        return;

    // 走意图层：GUI 取绘图数据与 CLI/脚本同一条路径，行为一致、可录制回放。
    Intent intent;
    intent.op = QStringLiteral("plot.channel");
    intent.params[QStringLiteral("dataset")] = datasetId;
    intent.params[QStringLiteral("table")] = table;
    intent.params[QStringLiteral("column")] = column;
    intent.params[QStringLiteral("px")] = qMax(64, m_canvas->width() - 80);

    const auto result = m_host->execute(intent);
    if (!result)
    {
        statusBar()->showMessage(QStringLiteral("渲染失败: %1").arg(result.error().message));
        return;
    }

    const QVariantMap payload = result.value().payload.toMap();
    const QVariantList xs = payload.value(QStringLiteral("x")).toList();
    const QVariantList mins = payload.value(QStringLiteral("min")).toList();
    const QVariantList maxs = payload.value(QStringLiteral("max")).toList();

    QVector<LodTriple> lod;
    lod.reserve(xs.size());
    for (int i = 0; i < xs.size(); ++i)
    {
        LodTriple triple;
        triple.x = xs.at(i).toDouble();
        triple.min = mins.value(i).toDouble();
        triple.max = maxs.value(i).toDouble();
        lod.append(triple);
    }

    m_canvas->setChannel(datasetId, table, column, payload.value(QStringLiteral("title")).toString());
    m_canvas->setLodData(lod, payload.value(QStringLiteral("t0")).toDouble(),
                          payload.value(QStringLiteral("t1")).toDouble());
    statusBar()->showMessage(QStringLiteral("显示 %1.%2（%3 行，抽稀至 %4 点）")
                                  .arg(table, column)
                                  .arg(t->rows)
                                  .arg(lod.size()));
}

} // namespace ng::gui
