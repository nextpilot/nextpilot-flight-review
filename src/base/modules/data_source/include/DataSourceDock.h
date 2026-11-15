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
        std::function<void(const QString &)> removeChannel;
        std::function<QVector<QStringList>()> sourceMsgs;
    };

    explicit DataSourceDock(Context context, QWidget *parent = nullptr);

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

private:
    void syncSrcChips();
    void applySrcFilter();
};

} // namespace ps::data_source
