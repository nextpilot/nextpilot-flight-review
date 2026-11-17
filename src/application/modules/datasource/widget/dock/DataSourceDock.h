#pragma once

#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <deque>
#include "DataSourceModel.h"

struct PsChannelInfo;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

namespace ps::data_source
{
class DataSourceService;
struct DataSource;
struct TransformRecipe;

// 数据源 Dock 内容：源列表、字段树、参数/消息表、空态。
// 数据模型与文件加载仍由宿主注入，本类只管视图与交互。
class DataSourceDock : public QWidget
{
public:
    struct Context
    {
        std::function<void(const QStringList &)> loadFiles;
        std::function<void(const QString &)> connectLiveSource;
        std::function<void(const PsChannelInfo *)> addChannel;
        std::function<void(const PsChannelInfo *)> setChannel;
        std::function<DataSource *(int)> sourceById;
        const DataSourceService *service = nullptr;
        std::function<QWidget *(int)> sourceRow;
        std::function<int()> activeSourceId;
        std::function<void()> refreshActiveSource;
        std::function<void(const QString &)> addTransformFor;
        std::function<void(double, const QString &, bool)> addTimeMarker;
        std::function<void(const QString &)> log;
        std::function<bool()> hasSources;
        std::function<std::deque<DataSource> &()> sources;
        std::function<void(const QString &)> removeChannel;
        std::function<QVector<QStringList>()> sourceMsgs;
        std::function<void(DataSource *, bool)> setSourcePaused;
        std::function<void(DataSource *)> openSourceDebug;
        std::function<void(DataSource *)> configureLiveSource;
        std::function<void(QWidget *)> removeSourceRow;
        std::function<void(QWidget *)> activateSource;
        std::function<void(QWidget *)> installRowEventFilter;
        std::function<void(int, QWidget *)> bindSourceRow;
        std::function<void(DataSource *, const QString &)> renameSource;
        std::function<void(DataSource *, int)> alignSource;
        std::function<void(DataSource *)> setSourceBase;
        std::function<void(DataSource *, double, bool)> setSourceOffset;
        std::function<void(DataSource *, DataSource *, bool, const QString &)> compareSources;
        std::function<void(DataSource *)> closeLiveSource;
        std::function<void(const QString &)> removeSourceFromCanvases;
        std::function<void(int)> setActiveSource;
        std::function<void()> clearSourceView;
        std::function<void()> refreshSourceView;
        std::function<void()> refreshPropTree;
        std::function<void(DataSource &, bool)> sourceLoaded;
        std::function<void(const QString &)> recordRecent;
        std::function<void(const QString &)> statusMessage;
        std::function<void(DataSource *)> liveSourceChanged;
        std::function<void(DataSource *)> applySourceView;
        std::function<void(QWidget *)> showSourceProps;
        std::function<void()> refreshStatusChip;
        std::function<QVector<QPair<QString, QString>>()> plottedChannelColors;
    };

    explicit DataSourceDock(Context context, QWidget *parent = nullptr);

    bool loadFile(const QString &path, bool interactive = true);
    void connectLiveSource(const QString &extensionId);
    bool openLiveInput(const QStringList &ids, const QString &config, DataSource &dst,
                       QString *fanoutNote, QString *err);
    void configureLiveSource(DataSource *src);
    void pollLiveSources();
    void refreshLiveSource(DataSource *src);
    void closeLiveSource(DataSource *src);
    void updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor);
    void switchSource(int id);
    void reloadChannels(DataSource *src);
    void reloadSource(DataSource *src);
    void refreshActiveSource();
    void activateSource(QWidget *row);
    void setSourcePaused(DataSource *src, bool paused);
    void exportActiveCsv();
    void exportActiveMcap(const QString &path, ps::data_source::DataSource *src);
    void showSourceMenu(QWidget *row, const QPoint &globalPos);
    void removeSourceRow(QWidget *row);
    void renderSourceData(const QVector<const PsChannelInfo *> &channels,
                          const QVector<QPair<QString, QString>> &params,
                          const QVector<QStringList> &msgs,
                          const QString &fmt, bool backed, int activeId);
    QWidget *addSourceRow(const QString &name, const QString &meta, const QString &dotColor,
                          bool backed, const QString &path, int srcId);
    void refreshSrcCount();
    void updateSrcStatus();
    void syncSrcChips();
    void applySrcFilter();

    Context m_ctx;
    QLabel *m_srcCount = nullptr;
    QVBoxLayout *m_srcRows = nullptr;
    QScrollArea *m_srcListScroll = nullptr;
    QTabBar *m_srcTabs = nullptr;
    QLineEdit *m_srcFilter = nullptr;
    QStackedWidget *m_srcStack = nullptr;
    QTreeWidget *m_srcTree = nullptr;
    QTableWidget *m_paramTable = nullptr;
    QTableWidget *m_msgTable = nullptr;
    QWidget *m_srcEmpty = nullptr;
    QVector<QToolButton *> m_srcChips;
};

} // namespace ps::data_source
