#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

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
struct SourceData;
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
        std::function<SourceData *(int)> sourceById;
        std::function<int()> activeSourceId;
        std::function<void()> refreshActiveSource;
        std::function<void(const QString &)> addTransformFor;
        std::function<void(double, const QString &, bool)> addTimeMarker;
        std::function<void(const QString &)> log;
        std::function<bool()> hasSources;
        std::function<QVector<SourceData> &()> sources;
        std::function<void(const QString &)> removeChannel;
        std::function<QVector<QStringList>()> sourceMsgs;
        std::function<void(SourceData *, bool)> setSourcePaused;
        std::function<void(SourceData *)> openSourceDebug;
        std::function<void(SourceData *)> configureLiveSource;
        std::function<void(QWidget *)> removeSourceRow;
        std::function<void(QWidget *)> activateSource;
        std::function<void(QWidget *)> installRowEventFilter;
        std::function<void(int, QWidget *)> bindSourceRow;
        std::function<void(const QString &)> reloadFile;
        std::function<void(SourceData *, const QString &)> renameSource;
        std::function<void(SourceData *, int)> alignSource;
        std::function<void(SourceData *)> setSourceBase;
        std::function<void(SourceData *, double, bool)> setSourceOffset;
        std::function<void(SourceData *, SourceData *, bool, const QString &)> compareSources;
        std::function<void(SourceData *)> closeLiveSource;
        std::function<void(const QString &)> removeSourceFromCanvases;
        std::function<void(int)> setActiveSource;
        std::function<void()> clearSourceView;
        std::function<void()> refreshSourceView;
        std::function<void()> refreshPropTree;
        std::function<void(SourceData &, bool)> sourceLoaded;
        std::function<void(const QString &)> recordRecent;
        std::function<void(const QString &)> statusMessage;
        std::function<void(SourceData *)> liveSourceChanged;
        std::function<void(SourceData *)> applySourceView;
        std::function<void(QWidget *)> showSourceProps;
        std::function<void()> refreshStatusChip;
    };

    explicit DataSourceDock(Context context, QWidget *parent = nullptr);

    bool loadFile(const QString &path, bool interactive = true);
    void connectLiveSource(const QString &extensionId);
    bool openLiveInput(const QStringList &ids, const QString &config, SourceData &dst,
                       QString *fanoutNote, QString *err);
    void configureLiveSource(SourceData *src);
    void pollLiveSources();
    void refreshLiveSource(SourceData *src);
    void closeLiveSource(SourceData *src);
    void updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor);
    void switchSource(int id);
    void reloadChannels(SourceData *src);
    void refreshActiveSource();
    void activateSource(QWidget *row);
    void setSourcePaused(SourceData *src, bool paused);
    void exportActiveCsv();
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
    QLabel *m_srcStatus = nullptr;
    QVBoxLayout *m_srcRows = nullptr;
    QToolButton *m_srcListToggle = nullptr;
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
