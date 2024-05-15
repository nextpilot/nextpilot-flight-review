

#pragma once

#include <QtPlugin>
#include <QMenu>
#include <QWidget>
#include <QDomDocument>

namespace nextpilot {

class DataLoadIfo {
    QString url;
};

class DataLoader : public QObject {
public:
    DataLoader() = default;

    virtual ~DataLoader() = default;
};

using DataLoaderPtr = std::shared_ptr<DataLoader>;

class DataLoaderFile : public DataLoader {
};

} // namespace nextpilot

QT_BEGIN_NAMESPACE
Q_DECLARE_INTERFACE(nextpilot::DataLoader, "nextpilot.plugin.dataloader")
QT_END_NAMESPACE
