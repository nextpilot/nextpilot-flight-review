#pragma once

#include "IMessageParser.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

struct MessageParserSpec
{
    QString id;
    QString protocolId;
    QString displayName;
    int priority = 0;
    QStringList fileExtensions;
    std::function<bool(const QByteArray &)> sniff;
    std::function<std::unique_ptr<IMessageParser>()> factory;
    QStringList descriptorPaths;
    QStringList capabilities;

    bool isValid() const
    {
        return !id.isEmpty() && !protocolId.isEmpty() && factory;
    }
};
