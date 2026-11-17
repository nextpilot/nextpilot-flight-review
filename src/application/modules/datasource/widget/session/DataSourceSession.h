#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QPair>
#include <QPoint>
#include <QAction>
#include <deque>
#include "TimeBookmark.h"
#include "DataSourceModel.h"

namespace ps::runtime { struct ModuleContext; }
class QDockWidget;
class QTreeWidget;
class QTabBar;
class QStackedWidget;
class QLabel;
class QTimer;
class QLineEdit;
class QTableWidget;
class QVBoxLayout;
class QScrollArea;
class QToolButton;
class QMenu;
class PlottingCanvas;
struct PsChannelInfo;
namespace ps::shell { struct PropertyContent; }
namespace ps::data_source { class DataSourceController; class DataSourceService; class DataSourceDock; struct DataSource; }

class DataSourceSession : public QWidget
{
public:
    explicit DataSourceSession(ps::runtime::ModuleContext &ctx);
    ~DataSourceSession() override;

    QWidget *createDataSourcePanel(QWidget *parent);
    void registerDataCommands();
    bool loadFile(const QString &path, bool interactive = true);
    void openPath(const QString &path);
    void connectLiveSource(const QString &extensionId);
    void connectSource(const QString &id);
    void closeSources();
    void addTimeMarkerForward(double t, const QString &label, bool prompt);
    void addTimeMarker(double t, const QString &label, bool prompt);
    void removeTimeMarker(const QString &id);
    void moveTimeMarker(const QString &id, double to);
    void renameTimeMarker(const QString &id, const QString &label);
    void onClose();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void pollLiveSources();
    void clearParseCache();
    void saveLayoutTo(const QString &path);
    void importLayoutFrom(const QString &path);
    void refreshCount();
    ps::data_source::DataSource *sourceById(int id);
    int activeSourceId() const;
    std::deque<ps::data_source::DataSource> &sources();
    const std::deque<ps::data_source::DataSource> &sources() const;
    const QVector<const PsChannelInfo *> &channels() const;
    QVector<ps::TimeBookmark> &timeMarkers();
    void refreshActiveSource();
    void reloadChannels(ps::data_source::DataSource *src);
    void activateSource(QWidget *row);
    void switchSource(int id);
    void setSourcePaused(ps::data_source::DataSource *src, bool paused);
    void exportActiveCsv();
    void showSourceProps(QWidget *row);
    ps::shell::PropertyContent sourceContent(int srcId);
    ps::shell::PropertyContent channelContent(int srcId, int index);
    void applySourceProp(int srcId, const QString &key, const QString &text);
    void refreshPropertyNodes();

    void showSourceMenu(QWidget *row, const QPoint &globalPos);
    void removeSourceRow(QWidget *row);
    void updateStatusChips();
    void renderSourceData();
    int sourceIdForRow(QWidget *row) const;
    QWidget *sourceRow(int id) const;
    void setSourceOffset(ps::data_source::DataSource *src, double offsetSec, bool refit);
    void alignSource(ps::data_source::DataSource *src, int mode);
    void setSourceBase(ps::data_source::DataSource *src);
    void compareSources(ps::data_source::DataSource *src, ps::data_source::DataSource *other,
                        bool all, const QString &pick);
    void openSourceDebug(ps::data_source::DataSource *src);
    void updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor);
    void addTransformFor(const QString &channel);
    QWidget *addSourceRow(const QString &name, const QString &meta, const QString &dotColor,
                          bool backed, const QString &path, int srcId);
    void refreshSrcCount();
    void syncSrcChips();
    void updateSrcStatus();
    void applySrcFilter();
    bool openLiveInput(const QStringList &ids, const QString &config, ps::data_source::DataSource &dst,
                       QString *fanoutNote, QString *err);
    void configureLiveSource(ps::data_source::DataSource *src);
    void refreshLiveSource(ps::data_source::DataSource *src);
    void closeLiveSource(ps::data_source::DataSource *src);
    void persistSourceConfigs();

private:
    void persistSourceConfigsSoon();
    void restoreSavedConfigs();
    void applySavedConfig(ps::data_source::DataSource &src);
    void log(const QString &text);
    void refreshPropTree(PlottingCanvas *c = nullptr);
    PlottingCanvas *currentCanvas() const;
    void newPageForward();
    QVector<PlottingCanvas *> pageCanvasesForward() const;

    ps::runtime::ModuleContext *m_ctx = nullptr;
    ps::data_source::DataSourceController *m_core = nullptr;
    ps::data_source::DataSourceService *m_service = nullptr;
    ps::data_source::DataSourceDock *m_dataSourceDock = nullptr;
    QTreeWidget *m_srcTree = nullptr;
    QTabBar *m_srcTabs = nullptr;
    QStackedWidget *m_srcStack = nullptr;
    QLabel *m_srcCount = nullptr;
    QTimer *m_liveTimer = nullptr;
    QTimer *m_configTimer = nullptr;
    QHash<QString, ps::data_source::DataSourceConfig> m_savedConfigs;
    qint64 m_liveUiMs = 0;
    bool m_viewBacked = false;
    QString m_srcFmt;
    QVector<QPair<QString, QString>> m_srcParams;
    QVector<QStringList> m_srcMsgs;
    QLineEdit *m_srcFilter = nullptr;
    QTableWidget *m_paramTable = nullptr;
    QTableWidget *m_msgTable = nullptr;
    QVBoxLayout *m_srcRows = nullptr;
    QScrollArea *m_srcListScroll = nullptr;
    QWidget *m_srcEmpty = nullptr;
    QVector<QToolButton *> m_srcChips;
    QAction *m_chipSource = nullptr;
    QAction *m_chipLod = nullptr;
};


namespace ps::components { DataSourceSession *dataSourceSession(ps::runtime::ModuleContext &ctx); }
