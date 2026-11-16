#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QPoint>
#include <QAction>
#include "TimeBookmark.h"

class MainWindow;
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
namespace ps::data_source { class DataSourceDock; struct SourceData; struct SourceStore; }

class DataSourceSession : public QWidget
{
public:
    explicit DataSourceSession(MainWindow *mw);
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
    ps::data_source::SourceData *sourceById(int id);
    int activeSourceId() const { return m_activeSource; }
    QVector<ps::data_source::SourceData> &sources();
    ps::data_source::SourceStore *store() { return m_sourceStore; }
    const QVector<const PsChannelInfo *> &channels() const { return m_srcChannels; }
    QVector<ps::TimeBookmark> &timeMarkers() { return m_timeMarkers; }
    void refreshActiveSource();
    void reloadChannels(ps::data_source::SourceData *src);
    void activateSource(QWidget *row);
    void switchSource(int id);
    void setSourcePaused(ps::data_source::SourceData *src, bool paused);
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
    void setSourceOffset(ps::data_source::SourceData *src, double offsetSec, bool refit);
    void alignSource(ps::data_source::SourceData *src, int mode);
    void setSourceBase(ps::data_source::SourceData *src);
    void compareSources(ps::data_source::SourceData *src, ps::data_source::SourceData *other,
                        bool all, const QString &pick);
    void openSourceDebug(ps::data_source::SourceData *src);
    void updateSourceMeta(QWidget *row, const QString &meta, const QString &dotColor);
    void addTransformFor(const QString &channel);
    QWidget *addSourceRow(const QString &name, const QString &meta, const QString &dotColor,
                          bool backed, const QString &path, int srcId);
    void refreshSrcCount();
    void syncSrcChips();
    void updateSrcStatus();
    void applySrcFilter();
    bool openLiveInput(const QStringList &ids, const QString &config, ps::data_source::SourceData &dst,
                       QString *fanoutNote, QString *err);
    void configureLiveSource(ps::data_source::SourceData *src);
    void refreshLiveSource(ps::data_source::SourceData *src);
    void closeLiveSource(ps::data_source::SourceData *src);

private:
    void log(const QString &text);
    void refreshPropTree(PlottingCanvas *c = nullptr);
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows);
    PlottingCanvas *currentCanvas() const;
    void newPageForward();
    QVector<PlottingCanvas *> pageCanvasesForward() const;

    MainWindow *m_mw = nullptr;
    ps::data_source::DataSourceDock *m_dataSourceDock = nullptr;
    QTreeWidget *m_srcTree = nullptr;
    QTabBar *m_srcTabs = nullptr;
    QStackedWidget *m_srcStack = nullptr;
    QLabel *m_srcCount = nullptr;
    QTimer *m_liveTimer = nullptr;
    qint64 m_liveUiMs = 0;
    bool m_viewBacked = false;
    QString m_srcFmt;
    QVector<const PsChannelInfo *> m_srcChannels;
    QVector<QPair<QString, QString>> m_srcParams;
    QVector<QStringList> m_srcMsgs;
    QLineEdit *m_srcFilter = nullptr;
    QTableWidget *m_paramTable = nullptr;
    QTableWidget *m_msgTable = nullptr;
    QVBoxLayout *m_srcRows = nullptr;
    QScrollArea *m_srcListScroll = nullptr;
    QWidget *m_srcEmpty = nullptr;
    QVector<QToolButton *> m_srcChips;
    QVector<ps::TimeBookmark> m_timeMarkers;
    ps::data_source::SourceStore *m_sourceStore = nullptr;
    int m_activeSource = -1;
    QAction *m_chipSource = nullptr;
    QAction *m_chipLod = nullptr;
    bool m_propTreeBuilding = false;
};


namespace ps::components { DataSourceSession *dataSourceSession(MainWindow *mw); }
