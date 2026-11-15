#pragma once

#include <QString>

class QWidget;

// 宿主窗口的可选业务接口；base 入口只调用这些动作，不依赖具体窗口类型。
class IAppWindow
{
public:
    virtual ~IAppWindow() = default;
    virtual QWidget *widget() = 0;
    virtual void openFile(const QString &path) = 0;
    virtual void openLayout(const QString &path) = 0;
    virtual QString defaultLayoutPath() const = 0;
};
