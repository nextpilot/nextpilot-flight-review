#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>

struct PsDataset;
struct PsChannelInfo;
class LiveSession;

namespace ps::data_source
{

// 变换配方：写入 .psl，重启/导入时按序重放
struct TransformRecipe
{
    QString kind;             // filter / quat
    QString input;
    QString filter;
    QJsonObject params;
    QString output;           // filter 输出名 / quat 输出主题
    QString quat[4];
    bool degrees = true;
    bool unwrap = true;
};

// 一个数据源：各自持有内核数据集，切源不释放，删除源才释放
struct SourceData
{
    int id = -1;
    QString name;
    QString alias;             // 图例前缀，默认文件名，可重命名
    QString path;
    QString fmt;
    double offsetSec = 0.0;    // 显示时间 = 原始时间 - offset；多源对齐用
    PsDataset *ds = nullptr;
    QVector<const PsChannelInfo *> channels;
    QVector<QPair<QString, QString>> params;
    QVector<QStringList> msgs;
    QStringList derived;       // 本源由变换产生的通道名，右键才允许删除
    QVector<TransformRecipe> recipes;
    QWidget *row = nullptr;

    // 实时源：ds 为空，通道视图由 LiveSession 持有；handle 归扩展所有。
    bool live = false;
    bool paused = false;       // 仅实时源：暂停轮询
    bool plotted = false;
    QString sourceId;
    QString config;
    void *handle = nullptr;
    std::shared_ptr<LiveSession> liveSession;
    QVector<QPair<QString, void *>> fanout; // 共享 transport 下的外部解析器句柄
};

// 数据源模型仓库：源生命周期与 id 分配都在数据源模块内，主窗口只做编排。
class SourceStore
{
public:
    QVector<SourceData> &items() { return m_items; }
    const QVector<SourceData> &items() const { return m_items; }
    SourceData *byId(int id)
    {
        for (SourceData &src : m_items)
            if (src.id == id)
                return &src;
        return nullptr;
    }
    int nextId() const
    {
        int id = 0;
        for (const SourceData &src : m_items)
            id = qMax(id, src.id + 1);
        return id;
    }
private:
    QVector<SourceData> m_items;
};

} // namespace ps::data_source
