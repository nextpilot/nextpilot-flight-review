#pragma once

#include <QJsonObject>
#include <QStringList>

struct PsDataset;
struct PsChannelInfo;

struct TransformDialogResult
{
    QStringList addedNames;
    QString inputName;
    QString kind;
    QString filterId;
    QJsonObject params;
    QString outputName;
    QString quat[4];
    bool degrees = true;
    bool unwrap = true;
};

// 业务组件实现，base 只读结果，不认识具体变换 UI。
class ITransformDialog
{
public:
    virtual ~ITransformDialog() = default;
    virtual TransformDialogResult transformResult() const = 0;
};
