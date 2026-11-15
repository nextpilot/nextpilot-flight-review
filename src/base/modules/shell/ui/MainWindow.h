#pragma once

#include <QMainWindow>
#include <QJsonObject>

#include "Component.h"
#include "ExtensionHost.h"
#include "CommandRegistry.h"
namespace ps::data_source { struct SourceData; struct TransformRecipe; struct SourceStore; }
using ps::data_source::SourceData;
using ps::data_source::TransformRecipe;

#include <memory>

struct PsDataset;
struct PsChannelInfo;
class SARibbonBar;
class SARibbonPanel;

class LiveSession;
class CanvasWidget;
namespace ps::plot { class PlotWorkspace; }
namespace ps::data_source { class DataSourceDock; }
class QSplitter;
class QLabel;
class QPlainTextEdit;
class QComboBox;
class QTabWidget;
class QTabBar;
class QTreeWidget;
class QTableWidget;
class QStackedWidget;
class QListWidget;
class QVBoxLayout;
class QLineEdit;
class QAction;
class QTimer;
class QScrollArea;
class QToolButton;

// 主窗口骨架（对应 §8 / D14）：
//   Ribbon（顶部任务页签） + 四个可停靠区（数据源 / 属性 / 脚本 / 日志） + 固定绘图区（页签=页）
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 打开一份 CSV（内核解析），把前 3 个通道画到当前页画布
    bool loadFile(const QString &path, bool interactive = true);
    void importLayoutFrom(const QString &path);
    void saveLayoutTo(const QString &path);
    void runScript(bool apply = false);
    QString defaultLayoutPath() const;
    void attachLogView(QPlainTextEdit *view);
    void bindScriptPanel(QPlainTextEdit *script, QComboBox *inputs[4], QTableWidget *params);
    void bindPlotPanels(QLabel *sel, QToolButton *edit, QTableWidget *table, QTreeWidget *tree);
    QDockWidget *createDataSourceDock(QWidget *parent, const QString &icon);
    QWidget *createDockTitleBar(QDockWidget *dock);
    void dockWidgetToArea(QDockWidget *dock, Qt::DockWidgetArea area);
    void collapseDockWidget(QDockWidget *dock);
    void showDockMenu(QToolButton *btn, QMenu *menu);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    CanvasWidget *currentCanvas() const;
    QWidget *pageForCanvas(CanvasWidget *c) const;
    QVector<CanvasWidget *> pageCanvases() const;
    QWidget *createRibbon();
    void registerCommands();
    void registerDataCommands();
    void registerPlotCommands();
    void applyPlayhead(double t);
    void clearParseCache();
    void initSourceRuntime();
    void reloadCanvasSettings();
    void createPlotPage(const QString &kind);
    void buildRibbonWidget(SARibbonPanel *panel, const QString &id, SARibbonBar *ribbon);
    void createDocks();
    void createPages();
    void log(const QString &text);
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows);
    void refreshPropTree(CanvasWidget *active = nullptr);
    void showAxesProps(CanvasWidget *c);
    void showSeriesProps(CanvasWidget *c, int index);
    void editSeriesProps(CanvasWidget *c, int index);
    void editAxesProps(CanvasWidget *c);
    QWidget *addSourceRow(const QString &name, const QString &meta, const QString &dotColor,
                          bool backed = false, const QString &path = QString(), int srcId = -1);
    void showSourceMenu(QWidget *row, const QPoint &globalPos);
    void removeSourceRow(QWidget *row);
    void activateSource(QWidget *row);   // 每个源各自一份字段/参数/消息视图
    void renderSourceData();
    void updateStatusChips();
    void refreshActiveSource();          // 变换写回后重建通道指针与字段树
    void reloadChannels(SourceData *src);
    void addTransformFor(const QString &channel);
    void connectLiveSource(const QString &extensionId);
    bool openLiveInput(const QStringList &ids, const QString &config, SourceData &dst,
                       QString *fanoutNote, QString *err);
    void configureLiveSource(SourceData *src);
    void openSourceDebug(SourceData *src);
    void pollLiveSources();
    void refreshLiveSource(SourceData *src);
    void closeLiveSource(SourceData *src);
    void updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor);
    void setSourceOffset(SourceData *src, double offsetSec, bool refit);
    void alignSource(SourceData *src, int mode);          // 0 起点 / 1 中心 / 2 终点
    void setSourceBase(SourceData *src);
    void compareSources(SourceData *src, SourceData *other, bool all, const QString &pick);
    void addCanvasToPage();
    void removeCanvas(CanvasWidget *c);
    void applyCanvasGrid(int rows, int cols);
    void splitCanvas(CanvasWidget *target, Qt::Orientation orientation);
    void toggleCanvasMaximize(CanvasWidget *c);
    void restoreCanvasMaximize();
    void exportActiveCsv();
    void openSettings();
    QJsonObject pageState(QWidget *page) const;
    QWidget *buildPage(const QJsonObject &o);
    int sourceIdForRow(QWidget *row) const;
    SourceData *sourceById(int id);
    void switchSource(int id);
    void refreshSrcCount();
    void applySrcFilter();
    void syncSrcChips();
    void updateSrcStatus();
    void showSourceProps(QWidget *row);
    void setSourcePaused(SourceData *src, bool paused);
public:
    ~MainWindow();
    void openPath(const QString &path);
    void connectSource(const QString &id);
    void setCanvasViewFlag(int flag, bool on);
    void canvasZoom(double factor);
    void canvasAutoY();
    void toggleDockMax(QDockWidget *dock);
    void restoreDockMax();
private:
    void addTimeMarker(double t, const QString &label, bool prompt);
    void removeTimeMarker(double t);
    void applyTimeMarkers();
    void newPage();

    QTabWidget *m_pages = nullptr;
    ps::plot::PlotWorkspace *m_plotWorkspace = nullptr;
    ps::data_source::DataSourceDock *m_dataSourceDock = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QTreeWidget *m_srcTree = nullptr;      // 字段页：通道树（按 topic 分组）
    QTabBar *m_srcTabs = nullptr;          // 字段 / 参数 / 消息
    QStackedWidget *m_srcStack = nullptr;
    QLabel *m_srcCount = nullptr;          // “0 源 · 0 已连接”
    QTimer *m_liveTimer = nullptr;
    qint64 m_liveUiMs = 0;
    bool m_viewBacked = false;            // 当前选中的数据源是否持有内核数据集
    QString m_srcFmt;                     // 当前源的格式标签（ULog/CSV/...）
    QVector<const PsChannelInfo *> m_srcChannels;
    QVector<QPair<QString, QString>> m_srcParams;
    QVector<QStringList> m_srcMsgs;
    QLineEdit *m_srcFilter = nullptr;      // 字段/参数/消息 共用的搜索框
    QString m_kernelInfo;                  // 状态栏常驻信息（Ribbon 悬停时临时替换）
    QTableWidget *m_paramTable = nullptr;
    QTableWidget *m_msgTable = nullptr;
    QVBoxLayout *m_srcRows = nullptr;
    QScrollArea *m_srcListScroll = nullptr;
    ps::components::ExtensionHost m_extensions;
    ps::commands::CommandRegistry m_commands;
    QWidget *m_srcEmpty = nullptr;
    QToolButton *m_srcListToggle = nullptr;
    QLabel *m_srcStatus = nullptr;
    QVector<QToolButton *> m_srcChips;
    QVector<QPair<double, QString>> m_timeMarkers;
    QTableWidget *m_propTable = nullptr;
    QToolButton *m_propEditBtn = nullptr;
    CanvasWidget *m_propCanvas = nullptr;
    int m_propSeriesIndex = -1;
    int m_propKind = 0;          // 1=坐标轴，2=曲线
    QTreeWidget *m_propTree = nullptr;
    bool m_propTreeBuilding = false;
    QLabel *m_selLabel = nullptr;
    QLabel *m_runLabel = nullptr;
    QPlainTextEdit *m_script = nullptr;
    QComboBox *m_scriptInputs[4] = {nullptr, nullptr, nullptr, nullptr};
    QTableWidget *m_scriptParams = nullptr;
    CanvasWidget *m_scriptPreviewCanvas = nullptr;
    QStringList m_scriptPreviewNames;
    ps::data_source::SourceStore *m_sourceStore = nullptr;
    int m_activeSource = -1;
    QAction *m_chipSource = nullptr;
    QAction *m_chipLod = nullptr;
    CanvasWidget *m_maxedCanvas = nullptr;
    QVector<QWidget *> m_maxedHidden;
    QDockWidget *m_maxDock = nullptr;
    QWidget *m_maxDockCentral = nullptr;
    QList<QDockWidget *> m_maxDockHidden;
    QVector<QPair<QSplitter *, QList<int>>> m_maxedSizes;
    QSize m_sizeBeforeMax;
};
